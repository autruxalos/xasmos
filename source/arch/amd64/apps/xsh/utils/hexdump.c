/******************************************************************************
 * XASMOS - Coreutil: hexdump. Shows the first 256 bytes of a file.
 * Path: source/arch/amd64/apps/xsh/utils/hexdump.c
 ******************************************************************************/
#include "common.h"
#include "exfs.h"

#define HEX_MAX 256u

static uint8_t g_buf[HEX_MAX];

int cmd_hexdump_main(const char *path) {
    int64_t n = exfs_read(path, g_buf, HEX_MAX);
    if (n < 0) return (int)n;
    if (n == 0) { xk_println("(empty)"); return EXFS_OK; }

    for (uint64_t off = 0; off < (uint64_t)n; off += 16) {
        put_hex(off, 8);
        xk_print("  ");
        for (uint64_t i = 0; i < 16; i++) {
            if (off + i < (uint64_t)n) put_hex(g_buf[off + i], 2);
            else                       xk_print("  ");
            xk_print(" ");
        }
        xk_print(" ");
        for (uint64_t i = 0; i < 16 && off + i < (uint64_t)n; i++) {
            uint8_t c = g_buf[off + i];
            char one[2] = { (c >= 32 && c <= 126) ? (char)c : '.', '\0' };
            xk_print(one);
        }
        xk_println("");
    }
    return EXFS_OK;
}
