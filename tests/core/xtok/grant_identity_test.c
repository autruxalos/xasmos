/* Host test for program-identity grants. A grant names a program by the full
 * SHA-512 digest of its .exb, a device, rights, and an optional block range.
 * Identity is the digest: a program with another digest gets nothing, and a
 * one-byte change in the image is a different program. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "capability.h"
#include "sha512.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define OWNER 1u

static uint8_t dig_q[SHA512_DIGEST_BYTES];   /* digest of queue_prog.exb */
static uint8_t dig_n[SHA512_DIGEST_BYTES];   /* digest of nd0_prog.exb   */
static uint8_t dig_x[SHA512_DIGEST_BYTES];   /* an unrelated program     */
static uint8_t dig_q2[SHA512_DIGEST_BYTES];  /* queue_prog with one byte changed */

static void make_digests(void) {
    static const uint8_t img_q[] = "queue_prog image bytes";
    static const uint8_t img_n[] = "nd0_prog image bytes";
    static const uint8_t img_x[] = "prog image bytes";
    static uint8_t img_q2[sizeof img_q];
    memcpy(img_q2, img_q, sizeof img_q);
    img_q2[5] ^= 0x01;                       /* one bit, one different program */
    sha512(img_q, sizeof img_q - 1, dig_q);
    sha512(img_n, sizeof img_n - 1, dig_n);
    sha512(img_x, sizeof img_x - 1, dig_x);
    sha512(img_q2, sizeof img_q2 - 1, dig_q2);
}

static void setup(void) {
    make_digests();
    xtok_grants_reset();
    /* The boot's policy, by identity: queue_prog reads whole rd0 (read+release),
     * nd0_prog reads a granted range of nd0 only. Nothing for dig_x. */
    xtok_grant_put_identity(dig_q, "rd0", OWNER, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, 0, 0);
    xtok_grant_put_identity(dig_n, "nd0", OWNER, XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE, 4096, 8);
}

static void test_identity_selects_the_grant(void) {
    setup();
    CHECK(xtok_grant_check_identity(dig_q, "rd0", OWNER, XTOK_RIGHT_READ, 0, 1) == 1);
    CHECK(xtok_grant_check_identity(dig_n, "nd0", OWNER, XTOK_RIGHT_READ, 4096, 1) == 1);
}

static void test_grant_does_not_cross_programs(void) {
    setup();
    /* nd0_prog holds nd0; queue_prog, same owner, must not borrow it. */
    CHECK(xtok_grant_check_identity(dig_q, "nd0", OWNER, XTOK_RIGHT_READ, 4096, 1) == 0);
    CHECK(xtok_grant_check_identity(dig_n, "rd0", OWNER, XTOK_RIGHT_READ, 0, 1) == 0);
}

static void test_changed_image_gets_nothing(void) {
    setup();
    /* A one-bit change in queue_prog is a different program: it inherits no grant. */
    CHECK(xtok_grant_check_identity(dig_q2, "rd0", OWNER, XTOK_RIGHT_READ, 0, 1) == 0);
    CHECK(xtok_grant_check_identity(dig_q2, "nd0", OWNER, XTOK_RIGHT_READ, 4096, 1) == 0);
}

static void test_unknown_program_gets_nothing(void) {
    setup();
    CHECK(xtok_grant_check_identity(dig_x, "rd0", OWNER, XTOK_RIGHT_READ, 0, 1) == 0);
    CHECK(xtok_grant_check_identity(dig_x, "nd0", OWNER, XTOK_RIGHT_READ, 4096, 1) == 0);
}

static void test_range_and_rights_still_checked(void) {
    setup();
    CHECK(xtok_grant_check_identity(dig_n, "nd0", OWNER, XTOK_RIGHT_READ, 4095, 1) == 0); /* before range */
    CHECK(xtok_grant_check_identity(dig_n, "nd0", OWNER, XTOK_RIGHT_READ, 4103, 2) == 0); /* past end */
    CHECK(xtok_grant_check_identity(dig_n, "nd0", OWNER, XTOK_RIGHT_WRITE, 4096, 1) == 0); /* not granted */
}

int main(void) {
    test_identity_selects_the_grant();
    test_grant_does_not_cross_programs();
    test_changed_image_gets_nothing();
    test_unknown_program_gets_nothing();
    test_range_and_rights_still_checked();
    printf("grant_identity: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
