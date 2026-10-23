/******************************************************************************
 * XASMOS - Run an owner's address space in ring 3 (preemptible, resumable)
 * Path: source|arch|amd64|kernel|xeret|proc.c
 *
 * xproc_run_ctx() behaves like a normal call: it "returns" when the program
 * exits, faults, yields or runs out of quantum. Entering saves the kernel's
 * callee-saved registers and stack pointer; leaving restores them, switches
 * CR3 back, and returns the reason. The kernel itself always runs with
 * interrupts disabled; they are enabled only inside ring 3, by the RFLAGS
 * loaded on iretq, so a timer tick can never interrupt kernel code.
 *
 * Not saved: xmm6-xmm15 (callee-saved in the MS ABI) and any user FPU/SSE
 * state. User code is not given SSE state yet.
 ******************************************************************************/
#include "xproc.h"
#include "xframe.h"
#include "diag.h"
#include "lapic.h"

/* Plain globals (not static): the asm below refers to them by name. */
UINT64 g_xproc_resume_rsp;
UINT64 g_xproc_kernel_cr3;

UINT64 xproc_enter_asm(UINT64 cr3, XContext *ctx);
void   xproc_leave_asm(UINT64 status) __attribute__((noreturn));

/* The asm reads XContext by byte offset: fail the build if the layout moves. */
typedef char xctx_size_check[(sizeof(XContext) == 144) ? 1 : -1];
typedef char xctx_rip_check[(__builtin_offsetof(XContext, rip) == 120) ? 1 : -1];
typedef char xctx_rflags_check[(__builtin_offsetof(XContext, rflags) == 136) ? 1 : -1];

__asm__(
    ".text\n"
    ".globl xproc_enter_asm\n"
    "xproc_enter_asm:\n"                    /* rcx = cr3, rdx = XContext*       */
    "    pushq %rbx\n"
    "    pushq %rbp\n"
    "    pushq %rdi\n"
    "    pushq %rsi\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, g_xproc_resume_rsp(%rip)\n"
    "    movq %rcx, %cr3\n"                 /* the owner's address space        */
    "    pushq $0x1B\n"                     /* SS  = user data, RPL 3           */
    "    pushq 128(%rdx)\n"                 /* RSP                              */
    "    pushq 136(%rdx)\n"                 /* RFLAGS                           */
    "    pushq $0x23\n"                     /* CS  = user code, RPL 3           */
    "    pushq 120(%rdx)\n"                 /* RIP                              */
    "    movq   8(%rdx), %rbx\n"
    "    movq  16(%rdx), %rcx\n"
    "    movq  32(%rdx), %rsi\n"
    "    movq  40(%rdx), %rdi\n"
    "    movq  48(%rdx), %rbp\n"
    "    movq  56(%rdx), %r8\n"
    "    movq  64(%rdx), %r9\n"
    "    movq  72(%rdx), %r10\n"
    "    movq  80(%rdx), %r11\n"
    "    movq  88(%rdx), %r12\n"
    "    movq  96(%rdx), %r13\n"
    "    movq 104(%rdx), %r14\n"
    "    movq 112(%rdx), %r15\n"
    "    movq   0(%rdx), %rax\n"
    "    movq  24(%rdx), %rdx\n"            /* last: rdx was the pointer        */
    "    iretq\n"
    "\n"
    ".globl xproc_leave_asm\n"
    "xproc_leave_asm:\n"                    /* rcx = status                     */
    "    movq g_xproc_resume_rsp(%rip), %rsp\n"
    "    movq g_xproc_kernel_cr3(%rip), %rax\n"
    "    movq %rax, %cr3\n"
    "    movq %rcx, %rax\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %rsi\n"
    "    popq %rdi\n"
    "    popq %rbp\n"
    "    popq %rbx\n"
    "    ret\n"
);

#define RFLAGS_IF        0x200ULL
#define RFLAGS_USER_KEEP 0xCD5ULL     /* CF PF AF ZF SF DF OF: the only bits a program owns */

static XProcResult *g_res;
static XContext    *g_ctx;
static UINT32       g_quantum_ticks;
static UINT64       g_irq_unexpected;
static UINT64       g_irq_last;

static void save_ctx(XContext *c, const ExcFrame *f) {
    c->rax = f->rax; c->rbx = f->rbx; c->rcx = f->rcx; c->rdx = f->rdx;
    c->rsi = f->rsi; c->rdi = f->rdi; c->rbp = f->rbp;
    c->r8  = f->r8;  c->r9  = f->r9;  c->r10 = f->r10; c->r11 = f->r11;
    c->r12 = f->r12; c->r13 = f->r13; c->r14 = f->r14; c->r15 = f->r15;
    c->rip = f->rip; c->rsp = f->rsp; c->rflags = f->rflags;
}

static void user_hook(ExcFrame *f, unsigned long long cr2) {
    if (!g_res) {
        /* A ring-3 trap with no run in progress: nothing sane to resume. */
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    if (f->vector == 0x80) {
        switch (f->rax) {
        case XSYS_EXIT:
            g_res->reason    = XPROC_EXIT;
            g_res->exit_code = f->rdi;
            g_res->rip       = f->rip;
            xproc_leave_asm(XPROC_EXIT);
        case XSYS_LOG:
            if (g_res->log_n < XPROC_LOG_MAX) g_res->log[g_res->log_n++] = f->rdi;
            f->rax = 0;
            return;
        case XSYS_YIELD:
            f->rax = 0;                          /* what the program sees on resume */
            save_ctx(g_ctx, f);
            g_res->reason = XPROC_YIELD;
            g_res->rip    = f->rip;
            xproc_leave_asm(XPROC_YIELD);
        default:
            f->rax = (unsigned long long)-1;     /* unknown syscall */
            return;
        }
    }

    g_res->reason = XPROC_FAULT;
    g_res->vector = f->vector;
    g_res->error  = f->error;
    g_res->rip    = f->rip;
    g_res->cr2    = cr2;
    xproc_leave_asm(XPROC_FAULT);
}

static void irq_hook(ExcFrame *f, unsigned long long cr2) {
    (void)cr2;

    if (f->vector == LAPIC_TIMER_VEC) {
        lapic_eoi();                             /* before anything else, or the LAPIC
                                                    never delivers another timer tick */
        if ((f->cs & 3) == 3 && g_res && g_ctx) {
            save_ctx(g_ctx, f);
            g_res->reason = XPROC_PREEMPT;
            g_res->rip    = f->rip;
            xproc_leave_asm(XPROC_PREEMPT);
        }
        return;                                  /* a stale tick seen from ring 0 */
    }
    if (f->vector == LAPIC_SPURIOUS_VEC) {
        return;                                  /* spurious: must NOT be EOI'd */
    }
    g_irq_unexpected++;
    g_irq_last = f->vector;
    lapic_eoi();
}

void xproc_init(void) {
    gdt_init();
    idt_init();                   /* rewrite entries with the NEW kernel code selector */
    idt_install_user_gates();
    idt_install_irq_gates();
    idt_set_hook(user_hook);
    idt_set_irq_hook(irq_hook);
}

void xctx_init(XContext *c, UINT64 entry, UINT64 user_rsp) {
    UINT64 *p = (UINT64 *)c;
    for (UINT64 i = 0; i < sizeof(XContext) / sizeof(UINT64); i++) p[i] = 0;
    c->rip    = entry;
    c->rsp    = user_rsp;
    c->rflags = 0x2;
}

int xproc_set_quantum_ms(UINT32 ms) {
    if (ms == 0) { g_quantum_ticks = 0; return 0; }
    UINT32 per_ms = lapic_ticks_per_ms();
    if (per_ms == 0) return -1;
    UINT64 t = (UINT64)ms * per_ms;
    if (t > 0xFFFFFFFFULL) return -1;
    g_quantum_ticks = (UINT32)t;
    return 0;
}

int xproc_run_ctx(XAspace *as, XContext *ctx, XProcResult *res) {
    if (!as || !as->live || !ctx || !res) return -1;

    res->reason = 0; res->exit_code = 0; res->vector = 0; res->error = 0;
    res->rip = 0; res->cr2 = 0; res->log_n = 0;
    for (int i = 0; i < XPROC_LOG_MAX; i++) res->log[i] = 0;

    /* The kernel never trusts flags it did not just compute: a program owns
     * only the arithmetic flags. IF is set exactly when a quantum is armed. */
    int armed = (g_quantum_ticks != 0);
    ctx->rflags = (ctx->rflags & RFLAGS_USER_KEEP) | 0x2 | (armed ? RFLAGS_IF : 0);

    g_res = res;
    g_ctx = ctx;
    g_xproc_kernel_cr3 = xpt_pml4_phys();

    if (armed) {
        lapic_timer_stop();
        /* Take any tick that fired after the last run ended, here in ring 0
         * where irq_hook just acknowledges it, so it cannot preempt the new
         * run before it has executed a single instruction. */
        __asm__ volatile ("sti; nop; nop; cli");
        lapic_timer_arm(g_quantum_ticks);
    }

    int r = (int)xproc_enter_asm(as->pml4_phys, ctx);

    if (armed) lapic_timer_stop();
    g_res = 0;
    g_ctx = 0;
    return r;
}

int xproc_run(XAspace *as, UINT64 entry, UINT64 user_rsp, XProcResult *res) {
    XContext ctx;
    xctx_init(&ctx, entry, user_rsp);
    return xproc_run_ctx(as, &ctx, res);
}

UINT64 xproc_unexpected_irqs(void)            { return g_irq_unexpected; }
UINT64 xproc_last_unexpected_vector(void)     { return g_irq_last; }
