/******************************************************************************
 * Regression harness: CPU time as a resource. Preemption by the LAPIC timer,
 * resumable contexts, yield, and the limits ring 3 cannot cross.
 * Path: tests/xeret/proc/preempt_test_main.c
 ******************************************************************************/
#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "paging.h"
#include "xproc.h"
#include "lapic.h"
#include "diag.h"

extern void fb_console_init(FramebufferInfo *fb);

#define UVA_CODE_N   0x8000000000
#define UVA_STACK_N  0x8000003000
#define LAPIC_VA_N   0xFEE00000
#define SUM_A_N      60000000
#define SUM_B_N      45000000
#define S2(x) #x
#define S(x)  S2(x)
#define UVA_CODE   ((UINT64)UVA_CODE_N)
#define UVA_STACK  ((UINT64)UVA_STACK_N)

__asm__(
    ".text\n"
    ".globl pb_spin\npb_spin:\n  1: jmp 1b\n.globl pb_spin_end\npb_spin_end:\n"

    ".globl pb_exit5\npb_exit5:\n  movq $5, %rdi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl pb_exit5_end\npb_exit5_end:\n"

    /* forge a timer tick from user space */
    ".globl pb_softint\npb_softint:\n  int $0x40\n  ud2\n.globl pb_softint_end\npb_softint_end:\n"

    ".globl pb_cli\npb_cli:\n  cli\n  ud2\n.globl pb_cli_end\npb_cli_end:\n"

    ".globl pb_lapic\npb_lapic:\n  movabsq $" S(LAPIC_VA_N) ", %rbx\n  movl (%rbx), %eax\n  ud2\n"
    ".globl pb_lapic_end\npb_lapic_end:\n"

    /* try to grant itself I/O privilege through the flags, then use a port */
    ".globl pb_iopl\npb_iopl:\n"
    "  pushq $0x3002\n  popfq\n"
    "  movw $0x3f8, %dx\n  movb $0x41, %al\n  outb %al, %dx\n  ud2\n"
    ".globl pb_iopl_end\npb_iopl_end:\n"

    /* three yields; the count lives only in a register */
    ".globl pb_yield3\npb_yield3:\n"
    "  xorl %ebx, %ebx\n"
    "  1: incq %rbx\n  movl $3, %eax\n  int $0x80\n  cmpq $3, %rbx\n  jne 1b\n"
    "  movq %rbx, %rdi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl pb_yield3_end\npb_yield3_end:\n"

    /* sum 1..N entirely in registers: any lost/corrupted register changes it */
    ".globl pb_sum_a\npb_sum_a:\n"
    "  movabsq $" S(SUM_A_N) ", %rcx\n  xorl %ebx, %ebx\n"
    "  1: addq %rcx, %rbx\n  decq %rcx\n  jnz 1b\n"
    "  movq %rbx, %rdi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl pb_sum_a_end\npb_sum_a_end:\n"

    ".globl pb_sum_b\npb_sum_b:\n"
    "  movabsq $" S(SUM_B_N) ", %rcx\n  xorl %ebx, %ebx\n"
    "  1: addq %rcx, %rbx\n  decq %rcx\n  jnz 1b\n"
    "  movq %rbx, %rdi\n  movl $1, %eax\n  int $0x80\n  ud2\n"
    ".globl pb_sum_b_end\npb_sum_b_end:\n"
);
extern char pb_spin[], pb_spin_end[], pb_exit5[], pb_exit5_end[], pb_cli[], pb_cli_end[];
extern char pb_softint[], pb_softint_end[];
extern char pb_lapic[], pb_lapic_end[], pb_iopl[], pb_iopl_end[], pb_yield3[], pb_yield3_end[];
extern char pb_sum_a[], pb_sum_a_end[], pb_sum_b[], pb_sum_b_end[];

static int fails;
static void check(const char *name, int ok) {
    serial_puts(ok ? "PASS  " : "FAIL  ");
    serial_puts(name);
    serial_puts("\n");
    if (!ok) fails++;
}

#define RW_RIGHTS (XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)
#define RX_RIGHTS (XTOK_RIGHT_READ | XTOK_RIGHT_EXECUTE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE)

typedef struct {
    XAspace   as;
    XContext  ctx;
    XToken    tCode, tStack;
    UINT64    code_phys;
    int       done;
    UINT64    result;
    int       preempts, yields;
} Proc;

static void load_code(UINT64 phys, const char *s, const char *e) {
    volatile UINT8 *d = (volatile UINT8 *)(UINTN)phys;
    for (int i = 0; i < 4096; i++) d[i] = 0;
    for (int i = 0; s + i < e; i++) d[i] = (UINT8)s[i];
}

/* An owner with a code page (R+X) and a stack page (R+W) running blob s..e. */
static int mkproc(Proc *p, UINT32 owner, const char *s, const char *e) {
    p->done = 0; p->result = 0; p->preempts = 0; p->yields = 0;
    if (xas_create(&p->as, owner) != XAS_OK) return -1;
    p->tCode  = xtok_alloc_ram(owner, 1, RX_RIGHTS);
    p->tStack = xtok_alloc_ram(owner, 1, RW_RIGHTS);
    UINT64 sp = 0;
    xtok_ram_info(p->tCode, &p->code_phys, 0);
    xtok_ram_info(p->tStack, &sp, 0);
    if (xas_map(&p->as, UVA_CODE,  p->tCode,  XPERM_R | XPERM_X) != XAS_OK) return -1;
    if (xas_map(&p->as, UVA_STACK, p->tStack, XPERM_R | XPERM_W) != XAS_OK) return -1;
    load_code(p->code_phys, s, e);
    xctx_init(&p->ctx, UVA_CODE, UVA_STACK + 4096);
    return 0;
}

static void killproc(Proc *p) {
    xas_destroy(&p->as);
    xtok_release(p->tCode);
    xtok_release(p->tStack);
}

static XProcResult R;

void kernel_main(XeretHandoff *hand) {
    fb_console_init(&hand->fb);
    diag_init();
    if (phys_init_from_handoff(hand) != 0) { serial_puts("FAIL  phys_init\n"); for (;;) __asm__ volatile ("cli; hlt"); }
    xtok_init();

    UINT64 fb_bytes = (UINT64)hand->fb.pixels_per_scanline * 4ULL * hand->fb.height;
    check("xpt_init_identity", xpt_init_identity(hand->phys_end, hand->fb.base, fb_bytes) == 0);
    xproc_init();
    UINT64 F0 = phys_free_count();

    /* ---------------- the clock itself ---------------- */
    check("LAPIC timer initialised and calibrated against the PIT", lapic_timer_init() == 0);
    serial_puts("      LAPIC ticks per ms: "); serial_hex(lapic_ticks_per_ms());
    serial_puts("  x2APIC: "); serial_hex((unsigned long long)lapic_x2apic()); serial_puts("\n");
    check("calibration is plausible (> 1000 ticks/ms)", lapic_ticks_per_ms() > 1000);
    check("set a 5 ms quantum", xproc_set_quantum_ms(5) == 0);

    Proc P;

    /* ---------------- an infinite loop no longer owns the machine --------- */
    check("create a process that spins forever", mkproc(&P, 1, pb_spin, pb_spin_end) == 0);
    check("run: the kernel gets the CPU back (PREEMPT)", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_PREEMPT);
    check("  it was stopped inside its own loop", R.rip == UVA_CODE);
    check("  and its context was saved at that instruction", P.ctx.rip == UVA_CODE);
    check("run again: preempted again (the timer is re-armed, EOI works)",
          xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_PREEMPT);
    killproc(&P);

    /* ---------------- no stale tick leaks into the next run --------------- */
    check("create a short program", mkproc(&P, 1, pb_exit5, pb_exit5_end) == 0);
    check("a short program right after a preemption runs to its exit", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_EXIT);
    check("  with its exit code", R.exit_code == 5);
    killproc(&P);

    /* ---------------- yield: registers survive the round trip -------------- */
    check("create the yield program", mkproc(&P, 1, pb_yield3, pb_yield3_end) == 0);
    int y = 0, ex = 0;
    UINT64 code = 0;
    for (int i = 0; i < 10 && !ex; i++) {
        int r = xproc_run_ctx(&P.as, &P.ctx, &R);
        if (r == XPROC_YIELD) y++;
        else if (r == XPROC_EXIT) { ex = 1; code = R.exit_code; }
        else break;
    }
    check("three yields, then a normal exit", y == 3 && ex);
    check("  the counter kept only in RBX survived every round trip", code == 3);
    killproc(&P);

    /* ---------------- ring 3 cannot touch the clock or the flags --------- */
    check("create: program that executes cli", mkproc(&P, 1, pb_cli, pb_cli_end) == 0);
    check("cli from ring 3 faults", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_FAULT);
    check("  #GP (cannot mask interrupts)", R.vector == 13);
    killproc(&P);

    check("create: program that forges a timer tick with int 0x40", mkproc(&P, 1, pb_softint, pb_softint_end) == 0);
    check("a software int to a hardware vector from ring 3 faults", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_FAULT);
    check("  #GP: only the CPU itself can raise IRQ vectors", R.vector == 13);
    killproc(&P);

    check("create: program that reads the LAPIC registers", mkproc(&P, 1, pb_lapic, pb_lapic_end) == 0);
    check("LAPIC MMIO from ring 3 faults", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_FAULT);
    check("  #PF, protection violation (error 5): the timer is kernel-only",
          R.vector == 14 && R.error == 5 && R.cr2 == LAPIC_VA_N);
    killproc(&P);

    check("create: program that sets IOPL=3 with popf, then uses a port", mkproc(&P, 1, pb_iopl, pb_iopl_end) == 0);
    check("raising IOPL from ring 3 does not grant port access", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_FAULT);
    check("  #GP, as if popf had changed nothing", R.vector == 13);
    killproc(&P);

    /* flags the kernel did not compute are never loaded into the CPU */
    check("create: yield program for the flags check", mkproc(&P, 1, pb_yield3, pb_yield3_end) == 0);
    P.ctx.rflags = 0x3000 /*IOPL=3*/ | 0x100 /*TF*/ | 0x4000 /*NT*/ | 0x1 /*CF*/;
    check("run with hostile flags in the context", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_YIELD);
    check("  IOPL, TF and NT were stripped before iretq",
          (P.ctx.rflags & (0x3000 | 0x100 | 0x4000)) == 0);
    killproc(&P);

    /* ---------------- two programs sharing the CPU ------------------------ */
    Proc A, B;
    check("create process A (sum 1..60,000,000 in registers)", mkproc(&A, 1, pb_sum_a, pb_sum_a_end) == 0);
    check("create process B (sum 1..45,000,000 in registers)", mkproc(&B, 2, pb_sum_b, pb_sum_b_end) == 0);
    Proc *procs[2] = { &A, &B };
    int bad = 0;
    for (int round = 0; round < 5000 && !(A.done && B.done); round++) {
        for (int i = 0; i < 2; i++) {
            Proc *p = procs[i];
            if (p->done) continue;
            int r = xproc_run_ctx(&p->as, &p->ctx, &R);
            if (r == XPROC_EXIT)         { p->done = 1; p->result = R.exit_code; }
            else if (r == XPROC_PREEMPT) p->preempts++;
            else                         { bad = 1; p->done = 1; }
        }
    }
    UINT64 expA = (UINT64)SUM_A_N * (SUM_A_N + 1ULL) / 2;
    UINT64 expB = (UINT64)SUM_B_N * (SUM_B_N + 1ULL) / 2;
    serial_puts("      preemptions  A="); serial_hex((unsigned long long)A.preempts);
    serial_puts("  B="); serial_hex((unsigned long long)B.preempts); serial_puts("\n");
    check("both processes finished without faulting", !bad && A.done && B.done);
    check("A was preempted several times", A.preempts >= 3);
    check("B was preempted several times", B.preempts >= 3);
    check("A's sum is exact after being interleaved with B", A.result == expA);
    check("B's sum is exact after being interleaved with A", B.result == expB);
    killproc(&A);
    killproc(&B);

    /* ---------------- stray hardware interrupts are absorbed -------------- */
    UINT64 before = xproc_unexpected_irqs();
    __asm__ volatile ("int $0x55");
    __asm__ volatile ("int $0xC8");     /* >= 128: exercises the long push encoding */
    check("two stray vectors were counted and the kernel carried on",
          xproc_unexpected_irqs() == before + 2);
    check("  the last one seen was 0xC8", xproc_last_unexpected_vector() == 0xC8);

    /* ---------------- quantum 0 restores the old behaviour ---------------- */
    check("disable preemption", xproc_set_quantum_ms(0) == 0);
    check("create a short program", mkproc(&P, 1, pb_exit5, pb_exit5_end) == 0);
    check("with no quantum, programs still run to completion", xproc_run_ctx(&P.as, &P.ctx, &R) == XPROC_EXIT && R.exit_code == 5);
    killproc(&P);

    check("every frame is back after all of that", phys_free_count() == F0);
    check("no live tokens remain", xtok_live_count() == 0);

    serial_puts(fails ? "RESULT: FAILURES\n" : "RESULT: ALL PASS\n");
    serial_hex((unsigned long long)fails); serial_puts(" failing\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
