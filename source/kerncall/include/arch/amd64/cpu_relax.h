#ifndef XASM_ARCH_CPU_RELAX_H
#define XASM_ARCH_CPU_RELAX_H
static inline void xarch_cpu_relax(void) {
    __asm__ volatile("pause");
}
#endif
