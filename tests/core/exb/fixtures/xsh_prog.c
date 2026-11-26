#include "prog_abi.h"

#define SYS_EXIT       1
#define SYS_ACCEPT     17
#define SYS_WRITE      18
#define SYS_READLINE   19
#define SYS_CLEAR      20
#define SYS_SHELL_CMD  21

static inline long long sc5(long long n, long long a, long long b, long long c, long long d, long long e, long long *out) { long long r, o; register long long r8 __asm__("r8") = e; __asm__ volatile("int $0x80" : "=a"(r), "=d"(o) : "a"(n), "D"(a), "S"(b), "d"(c), "c"(d), "r"(r8) : "memory"); if (out) *out = o; return r; }
PROG_ENTRY(xsh_entry);

static unsigned long long slen(const char *s) {
    unsigned long long n = 0;
    while (s[n]) n++;
    return n;
}

static void puts_sys(const char *s) {
    sc4(SYS_WRITE, (long long)(unsigned long long)s, (long long)slen(s), 0, 0, 0);
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void split_line(char *line, char **cmd, char **arg) {
    while (*line == ' ' || *line == '\t') line++;
    *cmd = line;
    while (*line && *line != ' ' && *line != '\t') line++;
    if (*line) {
        *line++ = 0;
        while (*line == ' ' || *line == '\t') line++;
    }
    *arg = line;
}

void xsh_entry(unsigned long long ep_bud, unsigned long long ep_rd0,
               unsigned long long ep_nd0, unsigned long long a3,
               unsigned long long a4, unsigned long long a5)
{
    (void)a3; (void)a4; (void)a5;
    long long bud = 0, rd0 = 0, nd0 = 0;
    char line[256];

    if (ep_bud) sc4(SYS_ACCEPT, (long long)ep_bud, 0, 0, 0, &bud);
    if (ep_rd0) sc4(SYS_ACCEPT, (long long)ep_rd0, 0, 0, 0, &rd0);
    if (ep_nd0) sc4(SYS_ACCEPT, (long long)ep_nd0, 0, 0, 0, &nd0);

    puts_sys("XASMOS Shell v0.05 (ring-3) -- 'ver' lists directives\n");

    for (;;) {
        char *cmd = 0, *arg = 0;
        puts_sys("xent:|user|xent@");
        long long n = sc4(SYS_READLINE, (long long)(unsigned long long)line, sizeof(line), 0, 0, 0);
        if (n <= 0) continue;
        split_line(line, &cmd, &arg);
        if (!cmd || !cmd[0]) continue;
        if (streq(cmd, "clear")) {
            sc4(SYS_CLEAR, 0, 0, 0, 0, 0);
            continue;
        }
        if (streq(cmd, "halt")) {
            puts_sys("Halting XASMOS.\n");
            sc4(SYS_EXIT, 0, 0, 0, 0, 0);
        }
        sc5(SYS_SHELL_CMD, (long long)(unsigned long long)cmd,
            (long long)(unsigned long long)arg, bud, rd0, nd0, 0);
    }
}
