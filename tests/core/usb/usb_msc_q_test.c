#include <stdio.h>
#include "usb_msc_q.h"
static int checks = 0, failures = 0;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
int main(void) {
    uint8_t cdb[16], cbw[31];
    usb_scsi_cdb_read10(cdb, 0x12345678u, 4u);
    CHECK(cdb[0] == SCSI_CMD_READ10 && cdb[2] == 0x12u && cdb[5] == 0x78u && cdb[8] == 4u);
    usb_msc_build_cbw(cbw, 0xCAFEBABEu, 2048u, USB_MSC_DIR_IN, 0u, cdb, 10u);
    CHECK(cbw[0] == 0x55u && cbw[1] == 0x53u && cbw[2] == 0x42u && cbw[3] == 0x43u);
    CHECK(cbw[12] == USB_MSC_DIR_IN && cbw[14] == 10u && cbw[15] == SCSI_CMD_READ10);
    uint8_t csw[13] = { 0x55,0x53,0x42,0x53, 0xBE,0xBA,0xFE,0xCA, 0,0,0,0, 0 };
    uint32_t res = 99u;
    CHECK(usb_msc_parse_csw(csw, 0xCAFEBABEu, &res) == 0 && res == 0u);
    CHECK(usb_msc_parse_csw(csw, 0x11111111u, &res) == -2);
    csw[12] = 1u;
    CHECK(usb_msc_parse_csw(csw, 0xCAFEBABEu, &res) < 0);
    uint8_t cap_resp[8] = { 0,0,0x1F,0xFF, 0,0,2,0 };
    uint64_t blks = 0; uint32_t bsz = 0;
    CHECK(usb_scsi_parse_capacity10(cap_resp, &blks, &bsz) == 0 && blks == 8192ULL && bsz == 512u);
    uint8_t desc[32] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 0x08, 0x06, 0x50, 0,
        7, 5, 0x81, 0x02, 0x00, 0x02, 0,
        7, 5, 0x02, 0x02, 0x00, 0x02, 0
    };
    UsbMscEndpoints ep;
    CHECK(usb_msc_find_endpoints(desc, 32u, &ep) == 0);
    CHECK(ep.cfg_val == 1u && ep.ep_in == 0x81u && ep.ep_out == 0x02u && ep.ep_in_mps == 512u && ep.ep_out_mps == 512u);
    printf("usb_msc_q: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
