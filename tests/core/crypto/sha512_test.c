/* Host test for SHA-512: FIPS 180-4 vectors, the million-'a' vector, and the
 * property that any split of the input gives the same digest. */
#include <stdio.h>
#include <string.h>
#include "sha512.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int hex_eq(const uint8_t *d, const char *hex) {
    for (unsigned i = 0; i < SHA512_DIGEST_BYTES; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return 0;
        if (d[i] != (uint8_t)v) return 0;
    }
    return 1;
}

static void test_nist_short(void) {
    uint8_t d[SHA512_DIGEST_BYTES];
    sha512("", 0, d);
    CHECK(hex_eq(d, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
                    "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"));
    sha512("abc", 3, d);
    CHECK(hex_eq(d, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                    "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
}

static void test_nist_896bit(void) {
    static const char msg[] =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    uint8_t d[SHA512_DIGEST_BYTES];
    sha512(msg, strlen(msg), d);
    CHECK(hex_eq(d, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
                    "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"));
}

static void test_million_a(void) {
    static uint8_t chunk[1000];
    memset(chunk, 'a', sizeof chunk);
    Sha512Ctx c;
    uint8_t d[SHA512_DIGEST_BYTES];
    sha512_init(&c);
    for (int i = 0; i < 1000; i++) sha512_update(&c, chunk, sizeof chunk);
    sha512_final(&c, d);
    CHECK(hex_eq(d, "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
                    "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b"));
}

/* Every split point gives the same digest as the one-shot call. This covers
 * the partial-block buffer and the padding that crosses a block boundary. */
static void test_split_invariance(void) {
    static uint8_t msg[300];
    for (int i = 0; i < 300; i++) msg[i] = (uint8_t)(i * 7 + 3);
    uint8_t ref[SHA512_DIGEST_BYTES], d[SHA512_DIGEST_BYTES];
    sha512(msg, sizeof msg, ref);
    for (size_t cut = 0; cut <= sizeof msg; cut++) {
        Sha512Ctx c;
        sha512_init(&c);
        sha512_update(&c, msg, cut);
        sha512_update(&c, msg + cut, sizeof msg - cut);
        sha512_final(&c, d);
        CHECK(memcmp(d, ref, SHA512_DIGEST_BYTES) == 0);
    }
}

/* Lengths around the padding boundary (111, 112, 113, 127, 128 bytes). */
static void test_padding_boundaries(void) {
    static const size_t lens[] = { 111, 112, 113, 127, 128, 129 };
    static uint8_t msg[129];
    for (size_t i = 0; i < sizeof msg; i++) msg[i] = (uint8_t)i;
    for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        uint8_t one[SHA512_DIGEST_BYTES], byte[SHA512_DIGEST_BYTES];
        sha512(msg, lens[k], one);
        Sha512Ctx c;
        sha512_init(&c);
        for (size_t i = 0; i < lens[k]; i++) sha512_update(&c, &msg[i], 1);
        sha512_final(&c, byte);
        CHECK(memcmp(one, byte, SHA512_DIGEST_BYTES) == 0);
    }
}

int main(void) {
    test_nist_short();
    test_nist_896bit();
    test_million_a();
    test_split_invariance();
    test_padding_boundaries();
    printf("sha512: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
