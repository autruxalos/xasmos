#include "usb_hid_q.h"
int usb_hid_find_boot_iface(const uint8_t *cfg, uint32_t cfg_len, uint8_t wanted_proto, UsbHidEndpoint *out) {
    if (!cfg || cfg_len < 9u || !out) return -1;
    uint32_t off = 0u; int in_hid = 0;
    UsbHidEndpoint cur = { cfg[5], 0u, 0u, 0u, 8u, 10u };
    while (off + 2u <= cfg_len) {
        uint8_t blen = cfg[off], btype = cfg[off + 1u];
        if (blen < 2u || off + blen > cfg_len) break;
        if (btype == 0x04u && blen >= 9u) {
            uint8_t ic = cfg[off+5u], is = cfg[off+6u], ip = cfg[off+7u];
            if (ic == USB_CLASS_HID && is == USB_SUBCLASS_BOOT && (wanted_proto == 0u || ip == wanted_proto) && (ip == 1u || ip == 2u)) {
                in_hid = 1; cur.iface_num = cfg[off+2u]; cur.protocol = ip; cur.ep_in = 0u;
            } else in_hid = 0;
        } else if (btype == 0x05u && blen >= 7u && in_hid) {
            uint8_t ad = cfg[off+2u], at = cfg[off+3u];
            if ((ad & 0x80u) && ((at & 3u) == 3u)) {
                cur.ep_in = ad;
                cur.ep_in_mps = (uint16_t)cfg[off+4u] | ((uint16_t)cfg[off+5u] << 8);
                if (!cur.ep_in_mps) cur.ep_in_mps = 8u;
                cur.interval = cfg[off+6u]; *out = cur; return 0;
            }
        }
        off += blen;
    }
    return -1;
}
static char hid_ascii(uint8_t c, int sh) {
    if (c >= 0x04u && c <= 0x1Du) return (char)('a' + (c - 4u) - (sh ? 32 : 0));
    if (c >= 0x1Eu && c <= 0x27u) return (sh ? "!@#$%^&*()" : "1234567890")[c - 0x1Eu];
    switch (c) {
        case 0x28u: return '\n'; case 0x29u: return 0x1B; case 0x2Au: return '\b';
        case 0x2Bu: return '\t'; case 0x2Cu: return ' ';
        case 0x2Du: return sh ? '_' : '-'; case 0x2Eu: return sh ? '+' : '=';
        case 0x2Fu: return sh ? '{' : '['; case 0x30u: return sh ? '}' : ']';
        case 0x31u: return sh ? '|' : '\\'; case 0x33u: return sh ? ':' : ';';
        case 0x36u: return sh ? '<' : ','; case 0x37u: return sh ? '>' : '.';
        case 0x38u: return sh ? '?' : '/'; default: return 0;
    }
}
int usb_hid_kbd_decode(UsbHidKbdState *st, const uint8_t rep[8], char *out, uint32_t max) {
    if (!st || !rep || !out || !max) return -1;
    int sh = (rep[0] & 0x22u) != 0u; uint32_t n = 0u;
    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t k = rep[i]; if (k < 4u) continue;
        int seen = 0; for (uint32_t j = 0u; j < 6u; j++) if (st->prev_keys[j] == k) seen = 1;
        if (!seen) { char ch = hid_ascii(k, sh); if (ch && n < max) out[n++] = ch; }
    }
    for (uint32_t j = 0u; j < 6u; j++) st->prev_keys[j] = rep[j+2u];
    return (int)n;
}
int usb_hid_mouse_decode(const uint8_t *rep, uint32_t len, UsbHidMouseEvent *out) {
    if (!rep || len < 3u || !out) return -1;
    out->buttons = rep[0] & 7u; out->dx = (int8_t)rep[1]; out->dy = (int8_t)rep[2];
    out->wheel = len >= 4u ? (int8_t)rep[3] : 0; return 0;
}
