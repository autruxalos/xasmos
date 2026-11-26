#ifndef XASM_USER_H
#define XASM_USER_H
typedef unsigned char uint8_t; typedef unsigned short uint16_t; typedef unsigned int uint32_t; typedef int int32_t; typedef unsigned long uint64_t; typedef long int64_t; typedef uint64_t uintptr_t; typedef uint64_t XToken;
#define XTOKEN_INVALID ((XToken)0)
#define XSYS_EXIT 1u
#define XSYS_YIELD 3u
#define XSYS_INFO 8u
#define XSYS_WRITE 18u
#define XSYS_READLINE 19u
#define XSYS_ALLOC  4u
#define XSYS_MAP    5u
#define XSYS_UNMAP  6u
#define XSYS_OFFER  16u
#define XSYS_ACCEPT 17u
#define XSYS_CLEAR  20u
#define XPERM_RW    3u
#define XTOK_RIGHT_READ   (1ULL << 0)
#define XTOK_RIGHT_WRITE  (1ULL << 1)
#define XTOK_RIGHT_GIVE   (1ULL << 5)
#define XTOK_RIGHT_MAP    (1ULL << 6)
#define XINFO_FRAMES_USED 0u
#define XINFO_TOKENS_USED 2u
#define SVC_MAGIC 0x5841534Du
typedef struct { uint32_t magic, target, status; int32_t rc; XToken cap0, cap1; char cmd[32], arg[160], out[256]; } XasmServiceMailbox;
static inline int64_t xsys3(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3) { int64_t r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory"); return r; }
static inline void xsys_exit(uint64_t c) { (void)xsys3(XSYS_EXIT, c, 0, 0); for (;;) {} }
static inline void xsys_yield(void) { (void)xsys3(XSYS_YIELD, 0, 0, 0); }
static inline int64_t xsys_info(uint64_t k) { return xsys3(XSYS_INFO, k, 0, 0); }
static inline int64_t xsys_write(XToken con, const char *s, uint64_t l) { return xsys3(XSYS_WRITE, (uint64_t)s, l, con); }
static inline int64_t xsys_readline(XToken con, char *b, uint64_t m) { return xsys3(XSYS_READLINE, (uint64_t)b, m, con); }
static inline int64_t xsys_clear(XToken con) { return xsys3(XSYS_CLEAR, 0, 0, con); }
static inline uint64_t u_strlen(const char *s) { uint64_t n = 0; while (s && s[n]) n++; return n; }
static inline int u_strcmp(const char *a, const char *b) { while (*a && (*a == *b)) { a++; b++; } return (int)(uint8_t)*a - (int)(uint8_t)*b; }
static inline void u_memset(void *d, uint8_t v, uint64_t n) { uint8_t *p = (uint8_t *)d; for (uint64_t i = 0; i < n; i++) p[i] = v; }
static inline void u_print(XToken con, const char *s) { (void)xsys_write(con, s, u_strlen(s)); }
static inline void u_println(XToken con, const char *s) { u_print(con, s); u_print(con, "\n"); }
static inline void u_print_hex(XToken con, uint64_t v, int d) { char b[20]; const char *hx = "0123456789ABCDEF"; for (int i = d - 1; i >= 0; i--) { b[i] = hx[v & 15u]; v >>= 4; } b[d] = 0; u_print(con, b); }
static inline int64_t xsys_alloc(uint64_t frames, uint64_t rights, XToken *tok_out) {
    int64_t r; uint64_t t = 0;
    __asm__ volatile("int $0x80" : "=a"(r), "=d"(t) : "a"(XSYS_ALLOC), "D"(frames), "S"(rights) : "rcx", "r11", "memory");
    if (tok_out) *tok_out = t;
    return r;
}
static inline int64_t xsys_map(XToken tok, uint64_t virt, uint64_t perms) {
    return xsys3(XSYS_MAP, tok, virt, perms);
}
static inline int64_t xsys_unmap(uint64_t virt) {
    return xsys3(XSYS_UNMAP, virt, 0, 0);
}
static inline int64_t xsys_offer(XToken tok, XToken ep_send) {
    return xsys3(XSYS_OFFER, tok, ep_send, 0);
}
static inline int64_t xsys_accept(XToken ep_recv, XToken *tok_out) {
    int64_t r; uint64_t t = 0;
    __asm__ volatile("int $0x80" : "=a"(r), "=d"(t) : "a"(XSYS_ACCEPT), "D"(ep_recv) : "rcx", "r11", "memory");
    if (tok_out) *tok_out = t;
    return r;
}
static inline void *memcpy(void *d, const void *s, uint64_t n) { uint8_t *a=(uint8_t*)d; const uint8_t *b=(const uint8_t*)s; for(uint64_t i=0;i<n;i++) a[i]=b[i]; return d; }
#endif
