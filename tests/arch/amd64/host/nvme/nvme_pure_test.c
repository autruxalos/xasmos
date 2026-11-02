/* Host test for the pure NVMe decoding helpers. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nvme.h"

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    uint8_t id[4096];

    /* Identify Namespace: NSZE 0x0000000000100000 (1 Mi blocks) */
    memset(id, 0, sizeof id);
    id[0] = 0x00; id[1] = 0x00; id[2] = 0x10; id[3] = 0x00;
    CHECK(nvme_nsze_from_identify(id) == 0x100000ULL, "NSZE read little-endian");

    /* FLBAS = 0, LBAF0.LBADS = 9 -> 512-byte sectors */
    memset(id, 0, sizeof id);
    id[26] = 0;
    id[128 + 2] = 9;
    CHECK(nvme_lbads_from_identify(id) == 9, "LBAF0 with LBADS 9 is 512 B");

    /* FLBAS selects format 1; format 0 is 512 B but must not be used */
    memset(id, 0, sizeof id);
    id[26] = 1;
    id[128 + 2] = 9;      /* LBAF0 */
    id[132 + 2] = 12;     /* LBAF1 */
    CHECK(nvme_lbads_from_identify(id) == 12, "FLBAS chooses LBAF1 (4 KiB)");

    /* LBADS outside 9..12 is refused */
    memset(id, 0, sizeof id);
    id[128 + 2] = 3;
    CHECK(nvme_lbads_from_identify(id) == 0, "LBADS 3 is refused");
    id[128 + 2] = 20;
    CHECK(nvme_lbads_from_identify(id) == 0, "LBADS 20 is refused");

    /* Completion phase: entry is new when its phase equals the expected one */
    CHECK(nvme_cqe_is_new(1u << 16, 1) == 1, "phase 1 matches expected 1");
    CHECK(nvme_cqe_is_new(0u, 1) == 0, "phase 0 does not match expected 1");
    CHECK(nvme_cqe_is_new(0u, 0) == 1, "phase 0 matches expected 0");

    /* Status: SC in bits 17..24, SCT in 25..27; phase bit excluded */
    CHECK(nvme_cqe_status((1u << 16) | 0u) == 0, "success with phase set is status 0");
    CHECK(nvme_cqe_status((1u << 17) | (1u << 16)) == 1, "SC=1 is reported");
    CHECK(nvme_cqe_status((2u << 25)) == (2u << 8), "SCT=2 lands in the high bits");

    printf("nvme pure: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
