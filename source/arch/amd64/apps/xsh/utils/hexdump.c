/* XASMOS - Coreutil: hexdump. Shows a file's bytes, 16 per line. The buffer
 * is static: 4 KiB on the stack would pull in __chkstk. */
#include "common.h"
#include <exfs.h>

extern int exfs_read_data(const char *path, char *out, int max);

static char g_buf[EXFS_DATA_MAX];

int cmd_hexdump_main(const char *path) {
    char line[96];
    int n = exfs_read_data(path, g_buf, EXFS_DATA_MAX);

    if (n == EXFS_ERR_NOTFILE) { xk_println("EXNOENT: not a file"); return -1; }
    if (n == EXFS_ERR_DENIED)  { xk_println("EXPERM: token refused the read"); return -1; }
    if (n < 0)                 { xk_println("EXIOERR: read failed"); return -1; }
    if (n == 0)                { xk_println("(empty)"); return 0; }

    for (int off = 0; off < n; off += 16) {
        size_t k = (size_t)(n - off < 16 ? n - off : 16);
        xl_hexdump_line(line, (unsigned long long)off,
                        (const unsigned char *)g_buf + off, k);
        xk_println(line);
    }
    return 0;
}
