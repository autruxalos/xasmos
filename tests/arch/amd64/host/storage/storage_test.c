/* Host test for storage.c. Scenario comes from argv[1]; each run checks one
 * path through storage_open(). Controller calls are stand-ins. */
#include <stdio.h>
#include <string.h>
#include "storage.h"
#include "pci.h"
#include "nvme.h"

static const char *scenario = "";
static int scan_calls, init_calls;

int pci_scan(void) { scan_calls++; return 0; }

static PciDev fake_dev;
const PciDev *pci_find(uint8_t c, uint8_t s, uint8_t p, int skip) {
    (void)c; (void)s; (void)p;
    if (strcmp(scenario, "nodev") == 0 || skip != 0) return 0;
    return &fake_dev;
}

int nvme_init(const PciDev *d, BlkDev *out) {
    (void)d;
    init_calls++;
    if (strcmp(scenario, "initfail") == 0) return BLK_E_IO;
    out->sector_size = 512;
    out->sector_count = 1024;
    return BLK_OK;
}

int nvme_controller_serial(char *out, uint32_t max) {
    (void)max;
    strcpy(out, strcmp(scenario, "other") == 0 ? "SAM1234" : "xasmos");
    return BLK_OK;
}

int main(int argc, char **argv) {
    scenario = argc > 1 ? argv[1] : "test";
    int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL  %s\n", msg); fails++; } else printf("ok    %s\n", msg); } while (0)

    int rc = storage_open();
    if (strcmp(scenario, "nodev") == 0) {
        CHECK(rc == STORAGE_ENODEV, "no device: STORAGE_ENODEV");
        CHECK(init_calls == 0, "no device: the driver is never called");
        CHECK(storage_device() == NULL, "no device: storage_device() is NULL");
        CHECK(storage_is_test_disk() == 0, "no device: not the test disk");
    } else if (strcmp(scenario, "initfail") == 0) {
        CHECK(rc == BLK_E_IO, "driver failure is reported with its code");
        CHECK(storage_device() == NULL, "driver failure: no device is published");
    } else {
        CHECK(rc == STORAGE_OK, "device found and opened");
        CHECK(storage_device() != NULL, "storage_device() returns the device");
        CHECK(storage_device()->sector_size == 512, "device geometry comes from the driver");
        CHECK(storage_is_test_disk() == (strcmp(scenario, "test") == 0),
              "test disk only when the serial is xasmos");
        int again = storage_open();
        CHECK(again == STORAGE_OK, "second open succeeds");
        CHECK(init_calls == 1, "the controller is initialised exactly once");
        CHECK(scan_calls == 1, "the bus is scanned exactly once");
    }
    printf("storage %s: %s\n", scenario, fails ? "FAILED" : "all ok");
    return fails != 0;
}
