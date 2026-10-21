/******************************************************************************
 * XASMOS - GDT + TSS (ring 0 / ring 3 segments, kernel entry stack, #DF stack)
 * Path: source|arch|amd64|kernel|xeret|gdt.c
 *
 *   0x08 kernel code   0x10 kernel data   0x18 user data   0x20 user code
 *   0x28 TSS (16 bytes)
 * User selectors with RPL 3:  data 0x1B, code 0x23.
 ******************************************************************************/
#include "xproc.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef struct __attribute__((packed)) {
    u32 r0;
    u64 rsp0, rsp1, rsp2;
    u64 r1;
    u64 ist[7];
    u64 r2;
    u16 r3;
    u16 iopb;
} Tss;

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} GdtPtr;

static Tss  g_tss;
static u64  g_gdt[7];
static u8   g_k_stack[16384]  __attribute__((aligned(16)));   /* RSP0: ring 3 -> 0 */
static u8   g_df_stack[8192]  __attribute__((aligned(16)));   /* IST1: #DF         */
static int  g_ready;

void gdt_init(void) {
    if (g_ready) return;    /* ltr on an already-busy TSS would #GP */

    g_gdt[0] = 0;
    g_gdt[1] = 0x00AF9A000000FFFFULL;     /* kernel code, 64-bit, DPL0 */
    g_gdt[2] = 0x00CF92000000FFFFULL;     /* kernel data, DPL0         */
    g_gdt[3] = 0x00CFF2000000FFFFULL;     /* user data,   DPL3         */
    g_gdt[4] = 0x00AFFA000000FFFFULL;     /* user code,   64-bit, DPL3 */

    u64 base  = (u64)(&g_tss);
    u64 limit = sizeof(Tss) - 1;
    g_gdt[5] = (limit & 0xFFFFULL)
             | ((base & 0xFFFFFFULL) << 16)
             | (0x89ULL << 40)                          /* present, available 64-bit TSS */
             | (((limit >> 16) & 0xFULL) << 48)
             | (((base >> 24) & 0xFFULL) << 56);
    g_gdt[6] = base >> 32;

    g_tss.iopb    = sizeof(Tss);          /* no I/O bitmap: ring 3 gets no ports */
    g_tss.rsp0    = (u64)(g_k_stack + sizeof(g_k_stack));
    g_tss.ist[0]  = (u64)(g_df_stack + sizeof(g_df_stack));

    GdtPtr gp;
    gp.limit = (u16)(sizeof(g_gdt) - 1);
    gp.base  = (u64)g_gdt;

    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        : : "m"(gp) : "rax", "memory");

    g_ready = 1;
}

int gdt_ready(void) { return g_ready; }
