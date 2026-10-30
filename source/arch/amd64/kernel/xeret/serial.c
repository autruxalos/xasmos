/* XASMOS - COM1 serial output (115200 8N1, polled). */
#include "diag.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned long long u64;

#define COM1 0x3F8

static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    /* Bounded wait: boards without a UART must not hang the kernel. */
    for (int i = 0; i < 100000; i++) {
        if (inb(COM1 + 5) & 0x20) break;
    }
    outb(COM1, (u8)c);
}

void serial_puts(const char *s) {
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n') serial_putc('\r');
        serial_putc(s[i]);
    }
}

void serial_hex(unsigned long long v) {
    static const char digits[] = "0123456789ABCDEF";
    serial_putc('0');
    serial_putc('x');
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(v >> shift) & 0xF]);
    }
}
