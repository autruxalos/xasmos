/******************************************************************************
 * XASMOS - Exception frame + IDT hook (shared by idt.c and proc.c)
 * Path: source|arch|amd64|kernel|xeret|xframe.h
 ******************************************************************************/
#ifndef XASMOS_XFRAME_H
#define XASMOS_XFRAME_H

/* Layout matches the push order in isr_common (lowest address first). */
typedef struct {
    unsigned long long r15, r14, r13, r12, r11, r10, r9, r8;
    unsigned long long rbp, rdi, rsi, rdx, rcx, rbx, rax;
    unsigned long long vector, error;
    unsigned long long rip, cs, rflags, rsp, ss;
} ExcFrame;

/* Called for vector 0x80 (syscall) and for any exception raised in ring 3.
 * May return (syscall) or never return (it abandons the faulting context). */
typedef void (*IdtHook)(ExcFrame *f, unsigned long long cr2);

void idt_set_hook(IdtHook h);

/* Hardware interrupts (vectors 32..255): hook is called with the frame; it
 * returns normally, or abandons the interrupted context. */
void idt_set_irq_hook(IdtHook h);
void idt_install_irq_gates(void);

/* Adds the DPL-3 syscall gate (int 0x80) and puts #DF on IST1. Requires the
 * TSS from gdt_init() to be loaded first. */
void idt_install_user_gates(void);

#endif
