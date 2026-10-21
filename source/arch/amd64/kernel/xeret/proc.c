/******************************************************************************
 * XASMOS - Run an owner's address space in ring 3
 * Path: source|arch|amd64|kernel|xeret|proc.c
 *
 * xproc_run() behaves like a normal call: it "returns" when the user program
 * does int 0x80/XSYS_EXIT, or faults. Entering saves the kernel's callee-saved
 * registers and stack pointer; leaving restores them, switches CR3 back, and
 * returns the reason. Nothing the user program does can bring the kernel down.
 *
 * Not saved: xmm6-xmm15 (callee-saved in the MS ABI). User code is not given
 * SSE state yet and the kernel does not hold values in those registers
 * across this call.
 ******************************************************************************/
#include "xproc.h"
#include "xframe.h"
#include "diag.h"

/* Plain globals (not static): the asm below refers to them by name. */
UINT64 g_xproc_resume_rsp;
UINT64 g_xproc_kernel_cr3;

UINT64 xproc_enter_asm(UINT64 cr3, UINT64 entry, UINT64 user_rsp);
void   xproc_leave_asm(UINT64 status) __attribute__((noreturn));

__asm__(
    ".text\n"
    ".globl xproc_enter_asm\n"
    "xproc_enter_asm:\n"
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
    "    pushq %r8\n"                       /* RSP = user stack                 */
    "    pushq $0x2\n"                      /* RFLAGS: IF=0, IOPL=0             */
    "    pushq $0x23\n"                     /* CS  = user code, RPL 3           */
    "    pushq %rdx\n"                      /* RIP = entry                      */
    "    xorl %eax, %eax\n"                 /* never leak kernel register state */
    "    xorl %ebx, %ebx\n"
    "    xorl %ecx, %ecx\n"
    "    xorl %edx, %edx\n"
    "    xorl %esi, %esi\n"
    "    xorl %edi, %edi\n"
    "    xorl %ebp, %ebp\n"
    "    xorl %r8d, %r8d\n"
    "    xorl %r9d, %r9d\n"
    "    xorl %r10d, %r10d\n"
    "    xorl %r11d, %r11d\n"
    "    xorl %r12d, %r12d\n"
    "    xorl %r13d, %r13d\n"
    "    xorl %r14d, %r14d\n"
    "    xorl %r15d, %r15d\n"
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

static XProcResult *g_res;

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

void xproc_init(void) {
    gdt_init();
    idt_init();                   /* rewrite entries with the NEW kernel code selector */
    idt_install_user_gates();
    idt_set_hook(user_hook);
}

int xproc_run(XAspace *as, UINT64 entry, UINT64 user_rsp, XProcResult *res) {
    if (!as || !as->live || !res) return -1;

    res->reason = 0; res->exit_code = 0; res->vector = 0; res->error = 0;
    res->rip = 0; res->cr2 = 0; res->log_n = 0;
    for (int i = 0; i < XPROC_LOG_MAX; i++) res->log[i] = 0;

    g_res = res;
    g_xproc_kernel_cr3 = xpt_pml4_phys();
    int r = (int)xproc_enter_asm(as->pml4_phys, entry, user_rsp);
    g_res = 0;
    return r;
}
