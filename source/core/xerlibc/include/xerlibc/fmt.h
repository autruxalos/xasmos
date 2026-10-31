/* xerlibc -- number and dump formatting (freestanding, no stdio).
 * Path: source/core/xerlibc/include/xerlibc/fmt.h
 *
 * Output buffers are NOT NUL-terminated by these functions unless noted.
 * Sizes: xl_u64_dec needs 20 bytes; xl_hexdump_line needs 96 bytes. */
#ifndef XERLIBC_FMT_H
#define XERLIBC_FMT_H

#include <xerlibc/string.h>

/* Writes v in decimal. Returns the number of characters written (1..20). */
size_t xl_u64_dec(char *out, unsigned long long v);

/* Writes the low `width` hex digits of v, uppercase, zero-padded.
 * width is clamped to 16. Returns width. */
size_t xl_u64_hex(char *out, unsigned long long v, unsigned width);

/* Writes one dump line for up to 16 bytes of b:
 *   "00000000  48 69 ...   |Hi..........|"
 * Output is NUL-terminated. Returns the length without the NUL. */
size_t xl_hexdump_line(char *out, unsigned long long off, const unsigned char *b, size_t n);

#endif
