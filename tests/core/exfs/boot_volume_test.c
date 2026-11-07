/* The boot volume: EXFS on a RAM block device, as the kernel will use it. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "exfs.h"
#include "ramdisk.h"

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static int list_n;
static void count_cb(const char *name, int type) { (void)name; (void)type; list_n++; }

int main(void) {
    const uint64_t bytes = 64ull << 20;          /* 64 MiB boot volume */
    uint8_t *mem = malloc(bytes);
    RamDisk rd;
    CHECK(mem != NULL, "allocate the boot volume memory");
    CHECK(ramdisk_init(&rd, mem, bytes, 512) == BLK_OK, "bind the RAM disk");
    CHECK(ramdisk_init(&rd, mem, 1000, 512) == BLK_E_ARG, "a size that is not a whole number of sectors is refused");
    CHECK(ramdisk_init(&rd, mem, bytes, 300) == BLK_E_ARG, "a sector size that is not a power of two is refused");
    CHECK(ramdisk_init(&rd, mem, bytes, 512) == BLK_OK, "rebind for the test");

    CHECK(exfs_mount(ramdisk_device(&rd)) == EXFS_ENOFS, "a fresh RAM volume is blank: mount refuses");
    CHECK(exfs_format(ramdisk_device(&rd)) == EXFS_OK, "boot: format the RAM volume");
    CHECK(exfs_stat("|user") == EXFS_TYPE_DIR && exfs_stat("|bin") == EXFS_TYPE_DIR, "system directories exist at boot");

    CHECK(exfs_create("|user|hello") == EXFS_OK, "create |user|hello");
    CHECK(exfs_write("|user|hello", "hola desde la RAM", 17) == EXFS_OK, "write the content");
    CHECK(exfs_commit() == EXFS_OK, "commit to the RAM volume");

    /* a second mount of the same memory: what a restart of the kernel would see
     * if the memory had survived; here it proves the commit is on the device */
    CHECK(exfs_mount(ramdisk_device(&rd)) == EXFS_OK, "mount the RAM volume again");
    char out[64];
    CHECK(exfs_read("|user|hello", out, sizeof out) == 17 && memcmp(out, "hola desde la RAM", 17) == 0,
          "content is on the RAM volume after a remount");

    list_n = 0;
    CHECK(exfs_list("|", count_cb) == 4 && list_n == 4, "the root lists the four system directories");
    CHECK(exfs_list("|user", count_cb) == 1, "|user lists the file");

    free(mem);
    printf("exfs boot volume: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
