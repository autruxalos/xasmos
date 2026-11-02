void _start(void) {
    register long rax __asm__("rax") = 1;
    register long rdi __asm__("rdi") = 7;
    __asm__ volatile ("int $0x80" : : "r"(rax), "r"(rdi) : "memory");
    for (;;) ;
}
