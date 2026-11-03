/* Host test for the device registry. */
#include <stdio.h>
#include <string.h>
#include "dev.h"

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

static int d_read(void *c, uint64_t l, uint32_t n, void *b) { (void)c; (void)l; (void)n; (void)b; return BLK_OK; }
static int d_write(void *c, uint64_t l, uint32_t n, const void *b) { (void)c; (void)l; (void)n; (void)b; return BLK_OK; }
static const BlkOps ops = { d_read, d_write, 0 };
static BlkDev nd = { 0, &ops, 512, 1024 };
static BlkDev rd = { 0, &ops, 512, 2048 };

static int listed;
static void count_cb(const char *name, BlkDev *d) { (void)name; (void)d; listed++; }

int main(void) {
    CHECK(dev_register("nd0", &nd) == DEV_OK, "register nd0");
    CHECK(dev_register("rd0", &rd) == DEV_OK, "register rd0");
    CHECK(dev_register("nd0", &rd) == DEV_EEXIST, "a duplicate name is refused");
    CHECK(dev_register("", &nd) == DEV_EINVAL, "an empty name is refused");
    CHECK(dev_register("sd0", 0) == DEV_EINVAL, "a null device is refused");
    CHECK(dev_lookup("|dev|nd0") == &nd, "lookup by full path finds nd0");
    CHECK(dev_lookup("|dev|rd0") == &rd, "lookup finds rd0");
    CHECK(dev_lookup("|dev|sd0") == 0, "an unregistered name is absent");
    CHECK(dev_lookup("nd0") == 0, "a bare name without |dev| is not a device path");
    CHECK(dev_lookup("|dev|nd") == 0, "a prefix of a name does not match");
    listed = 0;
    CHECK(dev_list(count_cb) == 2 && listed == 2, "dev_list reports both devices");
    printf("dev registry: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
