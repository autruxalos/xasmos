/* Host test for the pure BAR decoder. Vectors come from real BAR layouts. */
#include <stdio.h>
#include <stdint.h>
#include "pci.h"

static int passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    uint64_t base, size;
    int r;

    /* 32-bit memory BAR, 16 KiB: firmware 0xFEBF0000, sizing reads 0xFFFFC000 */
    r = pci_decode_bar(0xFEBF0000u, 0, 0xFFFFC000u, 0, &base, &size);
    CHECK(r == 0 && base == 0xFEBF0000ULL && size == 0x4000ULL, "32-bit BAR: base and 16 KiB size");

    /* 64-bit memory BAR, 16 KiB, below 4 GiB: the NVMe BAR0 from lspci (f6f00000) */
    r = pci_decode_bar(0xF6F00004u, 0, 0xFFFFC004u, 0xFFFFFFFFu, &base, &size);
    CHECK(r == 1 && base == 0xF6F00000ULL && size == 0x4000ULL, "64-bit BAR at f6f00000, 16 KiB");

    /* 64-bit memory BAR above 4 GiB: high half 1, low 0 -> 0x100000000 */
    r = pci_decode_bar(0x00000004u, 0x00000001u, 0xFFF00004u, 0xFFFFFFFFu, &base, &size);
    CHECK(r == 1 && base == 0x100000000ULL && size == 0x100000ULL, "64-bit BAR above 4 GiB, 1 MiB");

    /* 32-bit memory BAR, prefetchable bit set (bit 3): base ignores the flags */
    r = pci_decode_bar(0xD0000008u, 0, 0xF0000008u, 0, &base, &size);
    CHECK(r == 0 && base == 0xD0000000ULL && size == 0x10000000ULL, "prefetchable flag does not change the base");

    /* I/O BAR: refused */
    r = pci_decode_bar(0x0000E001u, 0, 0xFFFFFFFCu, 0, &base, &size);
    CHECK(r == -1, "I/O BAR is refused");

    /* unused BAR: sizing reads zero */
    r = pci_decode_bar(0, 0, 0, 0, &base, &size);
    CHECK(r == -1, "unused BAR is refused");

    /* 64-bit BAR whose sizing reads zero: refused, not a 2^64 size */
    r = pci_decode_bar(0x00000004u, 0, 0x00000004u, 0, &base, &size);
    CHECK(r == -1, "64-bit BAR with zero mask is refused");

    printf("pci decode: %d passed, %d failed\n", passes, fails);
    return fails != 0;
}
