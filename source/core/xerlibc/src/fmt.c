/* xerlibc -- number and dump formatting. Same build flags as mem.c. */
#include <xerlibc/fmt.h>

static const char hexdigits[] = "0123456789ABCDEF";

size_t xl_u64_dec(char *out, unsigned long long v) {
    char tmp[20];
    size_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    for (size_t i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

size_t xl_u64_hex(char *out, unsigned long long v, unsigned width) {
    if (width > 16) width = 16;
    for (unsigned i = 0; i < width; i++) {
        out[i] = hexdigits[(v >> (4 * (width - 1 - i))) & 0xF];
    }
    return width;
}

size_t xl_hexdump_line(char *out, unsigned long long off, const unsigned char *b, size_t n) {
    size_t p = 0;
    if (n > 16) n = 16;

    p += xl_u64_hex(out + p, off, 8);
    out[p++] = ' ';
    out[p++] = ' ';

    for (size_t i = 0; i < 16; i++) {
        if (i < n) {
            out[p++] = hexdigits[b[i] >> 4];
            out[p++] = hexdigits[b[i] & 0xF];
        } else {
            out[p++] = ' ';
            out[p++] = ' ';
        }
        out[p++] = ' ';
        if (i == 7) out[p++] = ' ';
    }

    out[p++] = '|';
    for (size_t i = 0; i < 16; i++) {
        if (i < n) {
            out[p++] = (b[i] >= 0x20 && b[i] < 0x7F) ? (char)b[i] : '.';
        } else {
            out[p++] = ' ';
        }
    }
    out[p++] = '|';
    out[p] = '\0';
    return p;
}
