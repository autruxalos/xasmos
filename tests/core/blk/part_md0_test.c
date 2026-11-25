#include <stdio.h>
#include <string.h>
#include "ramdisk.h"
#include "part.h"
#include "md0.h"
static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
int main(void) {
    static uint8_t img[32 * 512]; memset(img, 0, sizeof(img));
    RamDisk rd;
    CHECK(ramdisk_init(&rd, img, sizeof(img), 512u) == BLK_OK);
    img[510] = 0x55u; img[511] = 0xAAu; img[446 + 4] = 0x83u; img[446 + 8] = 4u; img[446 + 12] = 10u;
    PartTable pt;
    CHECK(part_probe(&rd.dev, &pt) == BLK_OK && pt.format == PART_FMT_MBR && pt.count == 1u);
    CHECK(pt.entries[0].start_lba == 4ULL && pt.entries[0].sector_count == 10ULL);
    PartSlice sl; BlkDev pdev;
    CHECK(part_open_slice(&rd.dev, &pt.entries[0], &sl, &pdev) == BLK_OK);
    uint8_t sec[512]; memset(sec, 0xAB, 512u);
    CHECK(blk_write(&pdev, 0ULL, 1u, sec) == BLK_OK && img[4 * 512] == 0xABu);
    CHECK(blk_read(&pdev, 10ULL, 1u, sec) == BLK_E_RANGE);
    memset(img, 0, sizeof(img)); memcpy(&img[512], "EFI PART", 8u);
    img[512 + 72] = 2u; img[512 + 80] = 4u; img[512 + 84] = 128u;
    uint8_t *ge = &img[2 * 512]; ge[0] = 0x28u; ge[32] = 8u; ge[40] = 23u;
    ge[56] = 'S'; ge[58] = 'Y'; ge[60] = 'S';
    CHECK(part_probe(&rd.dev, &pt) == BLK_OK && pt.format == PART_FMT_GPT && pt.count == 1u);
    CHECK(pt.entries[0].start_lba == 8ULL && pt.entries[0].sector_count == 16ULL && strcmp(pt.entries[0].name, "SYS") == 0);
    static uint8_t d0_buf[16 * 512], d1_buf[16 * 512];
    RamDisk r0, r1;
    CHECK(ramdisk_init(&r0, d0_buf, sizeof(d0_buf), 512u) == BLK_OK);
    CHECK(ramdisk_init(&r1, d1_buf, sizeof(d1_buf), 512u) == BLK_OK);
    BlkDev *mems[2] = { &r0.dev, &r1.dev }; Md0Array arr; BlkDev md;
    CHECK(md0_init(&arr, MD0_MODE_STRIPE, 2u, mems, 2u, &md) == BLK_OK && md.sector_count == 32ULL);
    memset(sec, 0x5Au, 512u);
    CHECK(blk_write(&md, 2ULL, 1u, sec) == BLK_OK && d1_buf[0] == 0x5Au);
    CHECK(md0_init(&arr, MD0_MODE_MIRROR, 2u, mems, 2u, &md) == BLK_OK && md.sector_count == 16ULL);
    memset(sec, 0x77u, 512u);
    CHECK(blk_write(&md, 1ULL, 1u, sec) == BLK_OK && d0_buf[512] == 0x77u && d1_buf[512] == 0x77u);
    printf("part_md0: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
