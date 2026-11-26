#include <assert.h>
#include <stdio.h>
#include "e1000_q.h"
int main(void) {
    assert(e1000_is_supported_device(0x8086u, 0x100Eu) == 1);
    assert(e1000_is_supported_device(0x1234u, 0x100Eu) == 0);
    uint8_t mac[6];
    e1000_decode_mac_ral_rah(0x56341252u, 0x8000AB78u, mac);
    assert(mac[0] == 0x52u && mac[5] == 0xABu);
    E1000TxDesc td;
    assert(e1000_prepare_tx_desc(&td, 0x100000ULL, 64u) == 0);
    assert(td.length == 64u && (td.cmd & E1000_TXD_CMD_EOP));
    E1000RxDesc rd = { 0x200000ULL, 128u, 0u, (uint8_t)(E1000_RXD_STAT_DD | E1000_RXD_STAT_EOP), 0u, 0u };
    uint16_t rlen = 0u;
    assert(e1000_check_rx_desc(&rd, &rlen) == 1 && rlen == 128u);
    puts("PASS: test_e1000");
    return 0;
}
