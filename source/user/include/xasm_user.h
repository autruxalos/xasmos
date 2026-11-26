#include "syscall_abi.h"
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
static inline int64_t xsys3(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2) { return xarch_sys3(nr, a0, a1, a2); }
static inline int64_t xsys_map(XToken tok, uint64_t virt, uint64_t perms) {
    return xsys3(XSYS_MAP, tok, virt, perms);
}
static inline int64_t xsys_unmap(uint64_t virt) {
    return xsys3(XSYS_UNMAP, virt, 0, 0);
}
static inline int64_t xsys_offer(XToken tok, XToken ep_send) {
    return xsys3(XSYS_OFFER, tok, ep_send, 0);
}
static inline int64_t xsys_accept(XToken ep_recv, XToken *tok_out) { return xarch_sys3_out1(XSYS_ACCEPT, ep_recv, 0, 0, (uint64_t *)tok_out); }
static inline void *memcpy(void *d, const void *s, uint64_t n) { uint8_t *a=(uint8_t*)d; const uint8_t *b=(const uint8_t*)s; for(uint64_t i=0;i<n;i++) a[i]=b[i]; return d; }
#endif
