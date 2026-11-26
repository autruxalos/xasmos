#ifndef XASMOS_USB_HID_Q_H
#define XASMOS_USB_HID_Q_H
#include <stdint.h>
#define USB_CLASS_HID 0x03u
#define USB_SUBCLASS_BOOT 0x01u
#define USB_PROTO_KEYBOARD 0x01u
#define USB_PROTO_MOUSE 0x02u
#define USB_HID_REQ_SET_PROTOCOL 0x0Bu
#define USB_HID_REQ_SET_IDLE 0x0Au
typedef struct { uint8_t cfg_val, iface_num, protocol, ep_in; uint16_t ep_in_mps; uint8_t interval; } UsbHidEndpoint;
typedef struct { uint8_t prev_keys[6]; } UsbHidKbdState;
typedef struct { uint8_t buttons; int8_t dx, dy, wheel; } UsbHidMouseEvent;
int usb_hid_find_boot_iface(const uint8_t *cfg, uint32_t cfg_len, uint8_t wanted_proto, UsbHidEndpoint *out);
int usb_hid_kbd_decode(UsbHidKbdState *st, const uint8_t report[8], char *out_chars, uint32_t max_chars);
int usb_hid_mouse_decode(const uint8_t *report, uint32_t len, UsbHidMouseEvent *out);
#endif
