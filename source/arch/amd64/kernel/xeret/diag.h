#ifndef XASMOS_DIAG_H
#define XASMOS_DIAG_H

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_hex(unsigned long long v);

void idt_init(void);

/* serial_init() + idt_init(). Call as early as possible in kernel_main. */
void diag_init(void);

/* Deliberate faults for testing the handlers: 1 #UD, 2 #DE, 3 #PF, 4 #GP */
void diag_trigger(int kind);

#endif
