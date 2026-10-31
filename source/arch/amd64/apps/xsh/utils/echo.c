/* XASMOS - Coreutil: echo. Prints its argument, unchanged. */
extern void xk_println(const char *s);

int cmd_echo_main(const char *text) {
    xk_println(text);
    return 0;
}
