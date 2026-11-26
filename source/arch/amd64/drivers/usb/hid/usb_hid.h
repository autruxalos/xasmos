#ifndef XASMOS_USB_HID_H
#define XASMOS_USB_HID_H
#include <stdint.h>
#include "usb_hid_q.h"
typedef struct { int kbd_active; uint32_t kbd_slot; uint8_t kbd_port, kbd_ep; int mouse_active; uint32_t mouse_slot; uint8_t mouse_port, mouse_ep; } UsbHidInfo;
int usb_hid_init(UsbHidInfo *out);
int usb_hid_get_info(UsbHidInfo *out);
int usb_hid_poll_char(char *ch_out);
#endif
