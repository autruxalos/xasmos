/******************************************************************************
 * XASMOS - SHA-512 (portable, no dependencies)
 * Path: source/core/crypto/sha512.h
 *
 * Used to identify an executable image by its content. The full 64-byte digest
 * is the identity; it is never truncated.
 ******************************************************************************/
#ifndef XASMOS_SHA512_H
#define XASMOS_SHA512_H

#include <stdint.h>
#include <stddef.h>

#define SHA512_DIGEST_BYTES 64u
#define SHA512_BLOCK_BYTES 128u

typedef struct {
    uint64_t h[8];
    uint8_t  buf[SHA512_BLOCK_BYTES];
    uint64_t total;      /* bytes absorbed so far */
    uint32_t blen;       /* bytes waiting in buf  */
} Sha512Ctx;

void sha512_init(Sha512Ctx *c);
void sha512_update(Sha512Ctx *c, const void *data, size_t len);
void sha512_final(Sha512Ctx *c, uint8_t out[SHA512_DIGEST_BYTES]);
void sha512(const void *data, size_t len, uint8_t out[SHA512_DIGEST_BYTES]);

#endif /* XASMOS_SHA512_H */
