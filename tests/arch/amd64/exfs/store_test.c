/* Host test for EXFS persistence. A RAM disk can fail any write on demand, and
 * every crash point must leave the last committed state intact.
 * Token ledger calls are stand-ins: frames are host memory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "exfs_internal.h"
#include "store.h"

/* ---- stand-ins for the kernel token ledger ----------------------------- */
#define FAKE_TOKS 256
static void *fake_mem[FAKE_TOKS];
static int   fake_live[FAKE_TOKS];

XToken xtok_alloc_ram(UINT32 owner, UINT64 frames, UINT32 rights) {
    (void)owner; (void)frames; (void)rights;
    for (int i = 0; i < FAKE_TOKS; i++) {
        if (!fake_live[i]) {
            fake_mem[i] = calloc(1, 4096);
            fake_live[i] = 1;
            return (XToken)(i + 1);
        }
    }
    return XTOKEN_INVALID;
}
int xtok_release(XToken t) {
    if (t == XTOKEN_INVALID || t > FAKE_TOKS || !fake_live[t - 1]) return -1;
    free(fake_mem[t - 1]);
    fake_mem[t - 1] = NULL;
    fake_live[t - 1] = 0;
    return 0;
}
int xtok_validate(XToken t, UINT32 right) {
    (void)right;
    return t != XTOKEN_INVALID && t <= FAKE_TOKS && fake_live[t - 1];
}
int xtok_ram_info(XToken t, UINT64 *phys, UINT64 *frames) {
    if (t == XTOKEN_INVALID || t > FAKE_TOKS || !fake_live[t - 1]) return -1;
    *phys = (UINT64)(UINTN)fake_mem[t - 1];
    if (frames) *frames = 1;
    return 0;
}

/* ---- the RAM disk -------------------------------------------------------- */
#define SS      512u
#define SECTORS 32768u                  /* 16 MiB = 4096 blocks */
static uint8_t disk[SECTORS * SS];
static int     writes_seen;
static int     fail_write_at = -1;      /* index of the write that fails, -1 = none */

static int d_read(void *c, uint64_t lba, uint32_t n, void *buf) {
    (void)c;
    if (lba + n > SECTORS) return BLK_E_RANGE;
    memcpy(buf, disk + lba * SS, (size_t)n * SS);
    return BLK_OK;
}
static int d_write(void *c, uint64_t lba, uint32_t n, const void *buf) {
    (void)c;
    if (lba + n > SECTORS) return BLK_E_RANGE;
    int k = writes_seen++;
    if (k == fail_write_at) return BLK_E_IO;
    memcpy(disk + lba * SS, buf, (size_t)n * SS);
    return BLK_OK;
}
static int d_flush(void *c) { (void)c; return BLK_OK; }
static const BlkOps d_ops = { d_read, d_write, d_flush };
static BlkDev dev = { 0, &d_ops, SS, SECTORS };

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static void reboot(void) {
    /* A power cycle: nothing in RAM survives; the disk is all there is. */
    exfs_clear();
    exfs_table_dirty = 0;
}

static int read_file(const char *path, char *out, int max) {
    int n = exfs_read_data(path, out, max);
    return n;
}

static void fresh_disk(void) {
    memset(disk, 0, sizeof disk);
    writes_seen = 0;
    fail_write_at = -1;
}

int main(void) {
    char buf[4200];

    /* 1. a blank disk does not mount */
    fresh_disk();
    CHECK(exfs_mount(&dev) == EXFS_ERR_NOFS, "blank disk: mount refuses, never formats");

    /* 2. format: the four system directories exist and are committed */
    fresh_disk();
    CHECK(exfs_format(&dev) == 0, "format succeeds");
    CHECK(exfs_mounted(), "formatted volume is mounted");
    CHECK(exfs_stat("|user") == TYPE_DIR && exfs_stat("|root") == TYPE_DIR &&
          exfs_stat("|bin") == TYPE_DIR && exfs_stat("|boot") == TYPE_DIR,
          "system directories |user |root |bin |boot exist after format");

    /* 3. content survives a power cycle */
    CHECK(exfs_create_object("|user|a", TYPE_FILE) == 0, "create |user|a");
    CHECK(exfs_write_data("|user|a", "hello") == 0, "write hello");
    CHECK(exfs_sync() == 0, "sync");
    reboot();
    CHECK(exfs_mount(&dev) == 0, "mount after reboot");
    CHECK(exfs_stat("|user|a") == TYPE_FILE, "file exists after reboot");
    CHECK(read_file("|user|a", buf, sizeof buf) == 5 && strcmp(buf, "hello") == 0,
          "content survives the reboot");
    CHECK(exfs_stat("|bin") == TYPE_DIR, "system directories survive the reboot");

    /* 4. binary content with NUL bytes, larger than a text string */
    uint8_t bin[300];
    for (int i = 0; i < 300; i++) bin[i] = (uint8_t)(i % 7 == 0 ? 0 : i);
    CHECK(exfs_create_object("|bin|prog", TYPE_FILE) == 0, "create |bin|prog");
    CHECK(exfs_write_bytes("|bin|prog", bin, 300) == 0, "write 300 binary bytes");
    CHECK(exfs_sync() == 0, "sync binary");
    reboot();
    CHECK(exfs_mount(&dev) == 0, "mount after binary sync");
    CHECK(exfs_size("|bin|prog") == 300, "binary size survives");
    CHECK(read_file("|bin|prog", buf, sizeof buf) == 300 && memcmp(buf, bin, 300) == 0,
          "binary content survives byte for byte");

    /* 5. crash before the superblock: the old content is what comes back */
    CHECK(exfs_write_data("|user|a", "world") == 0, "write world (uncommitted)");
    writes_seen = 0;
    fail_write_at = 1 + 9;                 /* data block = write 0, table = 1..9, superblock = 10 */
    CHECK(exfs_sync() != 0, "sync fails at the superblock write");
    fail_write_at = -1;
    reboot();
    CHECK(exfs_mount(&dev) == 0, "mount after the failed commit");
    CHECK(read_file("|user|a", buf, sizeof buf) == 5 && strcmp(buf, "hello") == 0,
          "crash before the commit point: previous content is intact");

    /* 6. crash during the content write: nothing changes */
    CHECK(exfs_write_data("|user|a", "world") == 0, "write world again");
    writes_seen = 0;
    fail_write_at = 0;
    CHECK(exfs_sync() != 0, "sync fails at the content write");
    fail_write_at = -1;
    reboot();
    CHECK(exfs_mount(&dev) == 0, "mount after the failed content write");
    CHECK(read_file("|user|a", buf, sizeof buf) == 5 && strcmp(buf, "hello") == 0,
          "crash during the content write: previous content is intact");

    /* 7. a deleted file keeps its block until the next commit */
    CHECK(exfs_create_object("|user|x", TYPE_FILE) == 0, "create |user|x");
    CHECK(exfs_write_data("|user|x", "xxxxx") == 0, "write xxxxx");
    CHECK(exfs_sync() == 0, "sync x");
    CHECK(exfs_delete_object("|user|x") == 0, "delete |user|x");
    CHECK(exfs_create_object("|user|y", TYPE_FILE) == 0, "create |user|y");
    CHECK(exfs_write_data("|user|y", "yyyy") == 0, "write yyyy");
    writes_seen = 0;
    fail_write_at = 1 + 9;
    CHECK(exfs_sync() != 0, "sync fails after the delete and the new file");
    fail_write_at = -1;
    reboot();
    CHECK(exfs_mount(&dev) == 0, "mount after the failed delete commit");
    CHECK(exfs_stat("|user|x") == TYPE_FILE && read_file("|user|x", buf, sizeof buf) == 5 &&
          strcmp(buf, "xxxxx") == 0, "deleted file is still intact on disk: its block was not reused");
    CHECK(exfs_stat("|user|y") == -1, "the uncommitted file does not exist after the crash");

    /* 8. a clean sync with nothing to do writes nothing */
    CHECK(exfs_sync() == 0, "sync with no changes");
    writes_seen = 0;
    CHECK(exfs_sync() == 0 && writes_seen == 0, "idle sync writes zero blocks");

    /* 9. a corrupt table is refused, never repaired silently */
    fresh_disk();
    CHECK(exfs_format(&dev) == 0, "format again");
    reboot();
    disk[1 * 4096 + 300] ^= 0x5A;          /* flip a byte in table area A */
    CHECK(exfs_mount(&dev) == EXFS_ERR_NOFS, "corrupt table: mount refuses");

    /* 10. a corrupt superblock is refused */
    fresh_disk();
    CHECK(exfs_format(&dev) == 0, "format for the superblock test");
    reboot();
    disk[0] ^= 0xFF;
    CHECK(exfs_mount(&dev) == EXFS_ERR_NOFS, "corrupt superblock: mount refuses");

    printf("exfs store: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
