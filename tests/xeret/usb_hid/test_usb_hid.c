#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb_hid_q.h"

int main(void) {
    uint8_t cfg[] = {
        9, 2, 34, 0, 1, 1, 0, 0xA0, 50,
        9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
        7, 5, 0x81, 0x03, 8, 0, 10
    };
    UsbHidEndpoint ep;
    assert(usb_hid_find_boot_iface(cfg, sizeof(cfg), USB_PROTO_KEYBOARD, &ep) == 0);
    assert(ep.ep_in == 0x81 && ep.ep_in_mps == 8 && ep.protocol == USB_PROTO_KEYBOARD);

    UsbHidKbdState st;
    memset(&st, 0, sizeof(st));
    uint8_t r1[8] = { 0x02, 0, 0x04, 0, 0, 0, 0, 0 }; /* Shift + A */
    char out[8];
    assert(usb_hid_kbd_decode(&st, r1, out, 8) == 1 && out[0] == 'A');
    /* Holding same key should not emit duplicate */
    assert(usb_hid_kbd_decode(&st, r1, out, 8) == 0);
    uint8_t r0[8] = { 0 };
    assert(usb_hid_kbd_decode(&st, r0, out, 8) == 0);

    uint8_t mrep[4] = { 0x01, (uint8_t)-5, 12, (uint8_t)-1 };
    UsbHidMouseEvent mev;
    assert(usb_hid_mouse_decode(mrep, 4, &mev) == 0);
    assert(mev.buttons == 1 && mev.dx == -5 && mev.dy == 12 && mev.wheel == -1);
puts("PASS: test_usb_hid");
    return 0;
}
