/******************************************************************************
 * XASMOS - Coreutil: pci. Lists the PCI devices found at boot, with their
 * memory BARs. Read-only: it reports what the firmware configured.
 * Path: source/arch/amd64/apps/xsh/utils/pci.c
 ******************************************************************************/
#include "common.h"
#include "../../../drivers/pci/pci.h"

int cmd_pci_main(void) {
    int n = pci_scan();
    if (n < 0) xk_println("warning: device table full, list is partial");

    for (int i = 0; i < pci_count(); i++) {
        const PciDev *d = pci_at(i);
        put_hex(d->bus, 2);
        xk_print(":");
        put_hex(d->dev, 2);
        xk_print(".");
        put_dec(d->fn);
        xk_print("  ");
        put_hex(d->vendor, 4);
        xk_print(":");
        put_hex(d->device, 4);
        xk_print("  class ");
        put_hex(d->class_code, 2);
        put_hex(d->subclass, 2);
        xk_print("/");
        put_hex(d->prog_if, 2);
        for (int b = 0; b < 6; b++) {
            if (d->bar_size[b] == 0) continue;
            xk_print("  bar");
            put_dec((unsigned long long)b);
            xk_print("=");
            put_hex(d->bar[b], 16);
            xk_print("/");
            put_hex(d->bar_size[b], 16);
        }
        xk_println("");
    }
    return 0;
}
