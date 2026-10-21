/******************************************************************************
 * XASMOS - Minimal IDT + CPU exception reporting (vectors 0-31)
 * Path: source|arch|amd64|kernel|xeret|idt.c
 *
 * Reports to COM1 first (cannot fault), then mirrors to the framebuffer
 * console. Fatal by design: every exception prints and halts.
 * Known limit: no TSS/IST yet, so a #DF caused by a broken stack can still
 * escalate to a triple fault. Add a TSS with an IST stack for #DF later.
 ******************************************************************************/
#include "diag.h"
#include "xframe.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

#ifndef DIAG_MIRROR_FB
#define DIAG_MIRROR_FB 1
#endif
#if DIAG_MIRROR_FB
extern void xk_print(const char *str);   /* from fb_console.c */
#endif


typedef struct __attribute__((packed)) {
    u16 off_lo;
    u16 selector;
    u8  ist;
    u8  attr;
    u16 off_mid;
    u32 off_hi;
    u32 zero;
} IdtEntry;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} IdtPtr;

static IdtEntry idt[256] __attribute__((aligned(16)));

extern char isr_stub_base[];
extern char isr_stub_syscall[];

static IdtHook g_hook;
void idt_set_hook(IdtHook h) { g_hook = h; }

/* 32 stubs, 16 bytes apart. Vectors that push a CPU error code skip the
 * dummy push so every frame has the same shape. */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".globl isr_stub_base\n"
    "isr_stub_base:\n"
    ".set vec, 0\n"
    ".rept 32\n"
    "    .balign 16\n"
    "    .if (vec != 8) && (vec != 10) && (vec != 11) && (vec != 12) && (vec != 13) && (vec != 14) && (vec != 17) && (vec != 21) && (vec != 29) && (vec != 30)\n"
    "        pushq $0\n"
    "    .endif\n"
    "    pushq $vec\n"
    "    jmp isr_common\n"
    "    .set vec, vec + 1\n"
    ".endr\n"
    "\n"
    ".balign 16\n"
    "isr_common:\n"
    "    pushq %rax\n"
    "    pushq %rbx\n"
    "    pushq %rcx\n"
    "    pushq %rdx\n"
    "    pushq %rsi\n"
    "    pushq %rdi\n"
    "    pushq %rbp\n"
    "    pushq %r8\n"
    "    pushq %r9\n"
    "    pushq %r10\n"
    "    pushq %r11\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    movq %rsp, %rcx\n"          /* arg0 = ExcFrame* (MS x64 ABI)  */
    "    movq %rsp, %rbp\n"
    "    andq $-16, %rsp\n"
    "    subq $32, %rsp\n"           /* shadow space                   */
    "    call exception_handler\n"
    "    movq %rbp, %rsp\n"
    "    popq %r15\n"
    "    popq %r14\n"
    "    popq %r13\n"
    "    popq %r12\n"
    "    popq %r11\n"
    "    popq %r10\n"
    "    popq %r9\n"
    "    popq %r8\n"
    "    popq %rbp\n"
    "    popq %rdi\n"
    "    popq %rsi\n"
    "    popq %rdx\n"
    "    popq %rcx\n"
    "    popq %rbx\n"
    "    popq %rax\n"
    "    addq $16, %rsp\n"           /* drop vector + error code       */
    "    iretq\n"
);

/* int 0x80: no CPU error code, so push a dummy one, then the vector. */
__asm__(
    ".text\n"
    ".balign 16\n"
    ".globl isr_stub_syscall\n"
    "isr_stub_syscall:\n"
    "    pushq $0\n"
    "    pushq $0x80\n"
    "    jmp isr_common\n"
);

static const char *const exc_names[32] = {
    "#DE divide error",          "#DB debug",
    "NMI",                       "#BP breakpoint",
    "#OF overflow",              "#BR bound range",
    "#UD invalid opcode",        "#NM device not available",
    "#DF double fault",          "coprocessor overrun",
    "#TS invalid TSS",           "#NP segment not present",
    "#SS stack fault",           "#GP general protection",
    "#PF page fault",            "reserved (15)",
    "#MF x87 fp error",          "#AC alignment check",
    "#MC machine check",         "#XM simd fp error",
    "#VE virtualization",        "#CP control protection",
    "reserved (22)",             "reserved (23)",
    "reserved (24)",             "reserved (25)",
    "reserved (26)",             "reserved (27)",
    "#HV hypervisor injection",  "#VC vmm communication",
    "#SX security",              "reserved (31)",
};

static void report(const char *s) {
    serial_puts(s);
#if DIAG_MIRROR_FB
    xk_print(s);
#endif
}

static void report_hex(u64 v) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(v >> (60 - 4 * i)) & 0xF];
    }
    buf[18] = '\0';
    report(buf);
}

static void report_reg(const char *name, u64 v) {
    report(name);
    report("=");
    report_hex(v);
    report("  ");
}

void exception_handler(ExcFrame *f) {
    u64 cr2, cr3;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* A syscall, or ANY exception raised in ring 3, is the owner's problem,
     * not the kernel's: hand it to the process layer instead of halting. */
    if (g_hook && (f->vector == 0x80 || (f->cs & 3) == 3)) {
        g_hook(f, cr2);
        return;
    }

    report("\n*** CPU EXCEPTION: ");
    report(f->vector < 32 ? exc_names[f->vector] : "unknown");
    report(" ***\n");

    report_reg("VEC", f->vector);
    report_reg("ERR", f->error);
    report("\n");
    report_reg("RIP", f->rip);
    report_reg("CS", f->cs);
    report("\n");
    report_reg("RFLAGS", f->rflags);
    report_reg("RSP", f->rsp);
    report("\n");

    if (f->vector == 14) {
        report_reg("CR2", cr2);
        report("\n  #PF: ");
        report((f->error & 1)  ? "protection-violation " : "page-not-present ");
        report((f->error & 2)  ? "write " : "read ");
        report((f->error & 4)  ? "user " : "supervisor ");
        if (f->error & 8)  report("reserved-bit-set ");
        if (f->error & 16) report("instruction-fetch ");
        report("\n");
    }

    report_reg("RAX", f->rax);
    report_reg("RBX", f->rbx);
    report("\n");
    report_reg("RCX", f->rcx);
    report_reg("RDX", f->rdx);
    report("\n");
    report_reg("RSI", f->rsi);
    report_reg("RDI", f->rdi);
    report("\n");
    report_reg("RBP", f->rbp);
    report_reg("CR3", cr3);
    report("\nSystem halted.\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void idt_init(void) {
    u64 cs;
    __asm__ volatile ("cli");
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));   /* reuse firmware's code segment */

    for (int i = 0; i < 32; i++) {
        u64 addr = (u64)(isr_stub_base + 16 * i);
        idt[i].off_lo   = (u16)(addr & 0xFFFF);
        idt[i].selector = (u16)cs;
        idt[i].ist      = 0;
        idt[i].attr     = 0x8E;                     /* present, DPL0, interrupt gate */
        idt[i].off_mid  = (u16)((addr >> 16) & 0xFFFF);
        idt[i].off_hi   = (u32)(addr >> 32);
        idt[i].zero     = 0;
    }

    IdtPtr p;
    p.limit = (u16)(sizeof(idt) - 1);
    p.base  = (u64)idt;
    __asm__ volatile ("lidt %0" : : "m"(p));
}

void idt_install_user_gates(void) {
    u64 cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    u64 addr = (u64)isr_stub_syscall;
    idt[0x80].off_lo   = (u16)(addr & 0xFFFF);
    idt[0x80].selector = (u16)cs;
    idt[0x80].ist      = 0;
    idt[0x80].attr     = 0xEE;                  /* present, DPL 3, interrupt gate */
    idt[0x80].off_mid  = (u16)((addr >> 16) & 0xFFFF);
    idt[0x80].off_hi   = (u32)(addr >> 32);
    idt[0x80].zero     = 0;

    idt[8].ist = 1;                             /* #DF runs on its own stack (TSS.IST1) */
}

void diag_init(void) {
    serial_init();
    idt_init();
    serial_puts("XDIAG: serial + IDT online\n");
}

void diag_trigger(int kind) {
    switch (kind) {
    case 1:
        __asm__ volatile ("ud2");
        break;
    case 2: {
        volatile int zero = 0;
        volatile int q = 1 / zero;
        (void)q;
        break;
    }
    case 3: {
        volatile u64 *p = (volatile u64 *)0xFFFF800000000000ULL;
        (void)*p;
        break;
    }
    case 4: {
        volatile u64 *p = (volatile u64 *)0x0000800000000000ULL;
        (void)*p;
        break;
    }
    default:
        break;
    }
}
