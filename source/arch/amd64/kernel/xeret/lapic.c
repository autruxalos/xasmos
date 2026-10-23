/******************************************************************************
 * XASMOS - LAPIC timer + PIT calibration
 * Path: source|arch|amd64|kernel|xeret|lapic.c
 ******************************************************************************/
#include "lapic.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

#define IA32_APIC_BASE   0x1Bu
#define APIC_BASE_X2     (1ULL << 10)
#define APIC_BASE_EN     (1ULL << 11)

#define REG_EOI          0x0B0u
#define REG_SVR          0x0F0u
#define REG_LVT_TIMER    0x320u
#define REG_TIMER_INIT   0x380u
#define REG_TIMER_CUR    0x390u
#define REG_TIMER_DIV    0x3E0u
#define LVT_MASKED       (1u << 16)
#define DIV_BY_16        0x3u

#define PIT_10MS_COUNT   11932u        /* 1,193,182 Hz * 10 ms */

static int           g_ready;
static int           g_x2;
static volatile u32 *g_mmio;
static u32           g_ticks_per_ms;

static inline void outb(u16 p, u8 v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }

static u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}
static void wrmsr(u32 msr, u64 v) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((u32)v), "d"((u32)(v >> 32)));
}

/* xAPIC registers are MMIO at offset reg; x2APIC exposes the same registers
 * as MSR 0x800 + (reg >> 4). */
static u32 lread(u32 reg) {
    if (g_x2) return (u32)rdmsr(0x800u + (reg >> 4));
    return g_mmio[reg >> 2];
}
static void lwrite(u32 reg, u32 v) {
    if (g_x2) wrmsr(0x800u + (reg >> 4), v);
    else      g_mmio[reg >> 2] = v;
}

/* Remap the PIC away from the exception vectors and mask every line. */
static void pic_mask_all(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFF); outb(0xA1, 0xFF);
}

/* Counts LAPIC ticks during one 10 ms PIT interval. */
static int calibrate(u32 *ticks_per_ms) {
    outb(0x61, (u8)((inb(0x61) & ~0x02) | 0x01));   /* gate on, speaker off */
    outb(0x43, 0xB0);                               /* ch2, lo/hi, mode 0   */

    lwrite(REG_TIMER_DIV, DIV_BY_16);
    lwrite(REG_LVT_TIMER, LVT_MASKED | LAPIC_TIMER_VEC);

    outb(0x42, (u8)(PIT_10MS_COUNT & 0xFF));
    outb(0x42, (u8)(PIT_10MS_COUNT >> 8));          /* the PIT starts counting here */
    lwrite(REG_TIMER_INIT, 0xFFFFFFFFu);

    int done = 0;
    for (u32 spin = 0; spin < 200000000u; spin++) {
        if (inb(0x61) & 0x20) { done = 1; break; }  /* OUT2 high: 10 ms elapsed */
    }
    u32 cur = lread(REG_TIMER_CUR);
    lwrite(REG_TIMER_INIT, 0);
    if (!done) return -1;

    u32 elapsed = 0xFFFFFFFFu - cur;
    if (elapsed < 1000u || elapsed > 0xFFFF0000u) return -1;   /* implausible */
    *ticks_per_ms = elapsed / 10u;
    return 0;
}

int lapic_timer_init(void) {
    u32 a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1u), "c"(0u));
    if (!(d & (1u << 9))) return -1;                 /* no local APIC */

    u64 base = rdmsr(IA32_APIC_BASE);
    if (!(base & APIC_BASE_EN)) {
        base |= APIC_BASE_EN;
        wrmsr(IA32_APIC_BASE, base);
    }
    g_x2 = (base & APIC_BASE_X2) != 0;

    if (!g_x2) {
        u64 addr = base & 0xFFFFF000ULL;
        /* Must lie inside the uncached MMIO window xpt_init_identity maps. */
        if (addr < 0xFEC00000ULL || addr >= 0x100000000ULL) return -1;
        g_mmio = (volatile u32 *)(unsigned long long)addr;
    }

    pic_mask_all();
    lwrite(REG_SVR, 0x100u | LAPIC_SPURIOUS_VEC);    /* software-enable */

    if (calibrate(&g_ticks_per_ms) != 0) return -1;
    g_ready = 1;
    return 0;
}

UINT32 lapic_ticks_per_ms(void) { return g_ticks_per_ms; }
int    lapic_x2apic(void)       { return g_x2; }

void lapic_timer_arm(UINT32 ticks) {
    if (!g_ready) return;
    lwrite(REG_TIMER_DIV, DIV_BY_16);
    lwrite(REG_LVT_TIMER, LAPIC_TIMER_VEC);          /* one-shot, unmasked */
    lwrite(REG_TIMER_INIT, ticks);
}

void lapic_timer_stop(void) {
    if (!g_ready) return;
    lwrite(REG_TIMER_INIT, 0);
    lwrite(REG_LVT_TIMER, LVT_MASKED | LAPIC_TIMER_VEC);
}

void lapic_eoi(void) {
    if (!g_ready) return;
    lwrite(REG_EOI, 0);
}
