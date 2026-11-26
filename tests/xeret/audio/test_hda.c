#include <assert.h>
#include <stdio.h>
#include "hda_q.h"
int main(void) {
    assert(hda_is_pci_hda(0x04u, 0x03u) == 1);
    assert(hda_is_pci_hda(0x02u, 0x00u) == 0);
    assert(hda_encode_verb(0u, 2u, 0xF00u, 0x00u) == 0x002F0000u);
    HdaBdlEntry e;
    assert(hda_prepare_bdl_entry(&e, 0x400000ULL, 4096u, 1) == 0);
    assert(e.addr == 0x400000ULL && e.length == 4096u && e.ioc == 1u);
    puts("PASS: test_hda");
    return 0;
}
