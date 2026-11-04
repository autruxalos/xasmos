/* Host test for the superblock v3 encoding. Every field is attacked, and every
 * byte of the checksummed area is corrupted in turn. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sb.h"

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static void sample(SbFields *f) {
    memset(f, 0, sizeof *f);
    f->gen = 7;                                  /* odd -> area 1 */
    f->itab = EXFS_ITAB_AREA1;
    f->nitab = EXFS_ITAB_BLOCKS;
    f->ninodes = EXFS_INODES;
    f->total = 16384;
    f->ngroups = 32;
    for (uint32_t g = 0; g < f->ngroups; g++) {
        f->slot[g] = (uint8_t)(g & 1);
        f->free[g] = 500 - g;
    }
}

int main(void) {
    SbFields a, b;
    uint8_t page[EXFS_BLOCK];
    sample(&a);

    CHECK(sb_encode(page, &a) == SB_OK, "encode a valid superblock");
    CHECK(sb_decode(page, &b) == SB_OK, "decode it back");
    CHECK(b.gen == a.gen && b.itab == a.itab && b.ngroups == a.ngroups && b.total == a.total,
          "scalar fields survive the round trip");
    CHECK(memcmp(b.slot, a.slot, sizeof a.slot) == 0 && memcmp(b.free, a.free, sizeof a.free) == 0,
          "per-group tables survive the round trip");

    /* the layout is the documented one */
    CHECK(page[8] == 3, "version is 3 at offset 8");
    CHECK(page[64 + 1] == 1 && page[64] == 0, "slot table starts at offset 64");
    CHECK(page[576 + 4] == (uint8_t)(499 & 0xFF), "free table starts at offset 576");

    /* a field that does not belong to this generation is refused */
    SbFields c = a;
    c.itab = EXFS_ITAB_AREA0;                     /* odd gen must use area 1 */
    CHECK(sb_encode(page, &c) == SB_EFIELD, "a table area that does not match the generation is refused");
    c = a; c.slot[3] = 2;
    CHECK(sb_encode(page, &c) == SB_EFIELD, "a slot other than 0 or 1 is refused");
    c = a; c.ngroups = 0;
    CHECK(sb_encode(page, &c) == SB_EFIELD, "zero groups is refused");
    c = a; c.ngroups = SB_MAX_GROUPS + 1;
    CHECK(sb_encode(page, &c) == SB_EFIELD, "more groups than the format holds is refused");
    c = a; c.slot[a.ngroups] = 1;
    CHECK(sb_encode(page, &c) == SB_EFIELD, "a slot set beyond the last group is refused");
    c = a; c.free[a.ngroups] = 1;
    CHECK(sb_encode(page, &c) == SB_EFIELD, "a free count set beyond the last group is refused");

    /* wrong magic and wrong version */
    CHECK(sb_encode(page, &a) == SB_OK, "encode again");
    uint8_t p2[EXFS_BLOCK];
    memcpy(p2, page, sizeof p2);
    p2[0] = 'Y';
    CHECK(sb_decode(p2, &b) == SB_EBAD, "a wrong magic is refused");
    memcpy(p2, page, sizeof p2);
    p2[8] = 2;
    CHECK(sb_decode(p2, &b) == SB_EBAD, "a version 2 superblock is refused");

    /* every byte of the checksummed area: one flip must be detected */
    int all_caught = 1;
    for (uint32_t i = 0; i < SB_CHECK_OFF; i++) {
        memcpy(p2, page, sizeof p2);
        p2[i] ^= 0x01;
        if (sb_decode(p2, &b) == SB_OK) { all_caught = 0; printf("  undetected at byte %u\n", i); break; }
    }
    CHECK(all_caught, "a single-bit flip at every byte of the checked area is detected");

    /* a flip in the checksum itself is detected too */
    memcpy(p2, page, sizeof p2);
    p2[SB_CHECK_OFF] ^= 0x80;
    CHECK(sb_decode(p2, &b) == SB_ECHECK, "a damaged checksum is refused");

    /* a torn write: the second half of the block is old, first half new */
    SbFields d = a;
    d.gen = 8; d.itab = EXFS_ITAB_AREA0;
    uint8_t newpage[EXFS_BLOCK];
    CHECK(sb_encode(newpage, &d) == SB_OK, "encode the next generation");
    uint8_t torn[EXFS_BLOCK];
    memcpy(torn, newpage, sizeof torn);
    memcpy(torn + 2048, page + 2048, sizeof torn - 2048);
    CHECK(sb_decode(torn, &b) == SB_ECHECK, "a torn superblock is refused, not half-read");

    /* random corruption of random bytes never decodes to different fields */
    uint32_t rs = 99;
    int silent = 0;
    for (int t = 0; t < 2000; t++) {
        memcpy(p2, page, sizeof p2);
        for (int k = 0; k < 3; k++) {
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
            p2[rs % SB_CHECK_OFF] ^= (uint8_t)(rs >> 8) | 1;
        }
        SbFields r;
        if (sb_decode(p2, &r) == SB_OK && (r.gen != a.gen || memcmp(r.free, a.free, sizeof a.free) != 0)) silent++;
    }
    CHECK(silent == 0, "2000 random multi-byte corruptions never decode to different fields");

    printf("superblock v3: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
