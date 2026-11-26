#include <stdio.h>
#include "capability.h"
#include "owner.h"
#include "blk.h"
static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
static int nop_r(void *c, uint64_t l, uint32_t n, void *b) { (void)c;(void)l;(void)n;(void)b; return BLK_OK; }
static int nop_w(void *c, uint64_t l, uint32_t n, const void *b) { (void)c;(void)l;(void)n;(void)b; return BLK_OK; }
static int nop_f(void *c) { (void)c; return BLK_OK; }
static const BlkOps g_ops = { nop_r, nop_w, nop_f, 0 };
static BlkDev g_ser = { 0, &g_ops, 1u, 1024ULL };
static BlkDev g_fb  = { 0, &g_ops, 4096u, 512ULL };
int main(void) {
    owner_init(); xtok_init(); xtok_roots_reset();
    xtok_con_register_devs(&g_ser, &g_fb);
    UINT32 boot_o = owner_new(), init_o = owner_new(), shell_o = owner_new(), rogue_o = owner_new();
    XToken r_ser = xtok_alloc_blocks(boot_o, &g_ser, 0ULL, 1024ULL, XTOK_RIGHTS_BLOCKS_DEFAULT | XTOK_RIGHT_GIVE);
    XToken r_fb  = xtok_alloc_blocks(boot_o, &g_fb,  0ULL, 512ULL,  XTOK_RIGHTS_BLOCKS_DEFAULT | XTOK_RIGHT_MAP | XTOK_RIGHT_GIVE);
    CHECK(r_ser != XTOKEN_INVALID && r_fb != XTOKEN_INVALID);
    CHECK(xtok_give(boot_o, r_ser, init_o) == 0 && xtok_give(boot_o, r_fb, init_o) == 0);
    XToken s_ser = xtok_derive_blocks(init_o, r_ser, 0ULL, 1024ULL, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_GIVE);
    XToken s_fb  = xtok_derive_blocks(init_o, r_fb,  0ULL, 512ULL,  XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | XTOK_RIGHT_GIVE);
    CHECK(s_ser != XTOKEN_INVALID && s_fb != XTOKEN_INVALID);
    CHECK(xtok_give(init_o, s_ser, shell_o) == 0 && xtok_give(init_o, s_fb, shell_o) == 0);
    xtok_con_bind_session(shell_o, s_ser, s_fb);
    CHECK(xtok_con_authorize(shell_o, s_ser, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE) == 0);
    CHECK(xtok_con_authorize(shell_o, s_fb, XTOK_RIGHT_WRITE) == 0);
    CHECK(xtok_con_authorize(shell_o, XTOKEN_INVALID, XTOK_RIGHT_WRITE) == 0);
    CHECK(xtok_con_authorize(rogue_o, s_ser, XTOK_RIGHT_WRITE) != 0);
    CHECK(xtok_con_authorize(rogue_o, XTOKEN_INVALID, XTOK_RIGHT_WRITE) != 0);
    CHECK(xtok_revoke(r_ser, init_o) == 0 && xtok_revoke(r_fb, init_o) == 0);
    CHECK(xtok_con_authorize(shell_o, s_ser, XTOK_RIGHT_WRITE) != 0);
    CHECK(xtok_con_authorize(shell_o, XTOKEN_INVALID, XTOK_RIGHT_WRITE) != 0);
    printf("con_token: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
