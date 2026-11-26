#include "xasm_user.h"
#include "xasm_ipc_proto.h"

#define IPC_MAP_VA 0x0000008000040000ULL

static void scpy(char *d, const char *s, uint64_t m) {
    uint64_t i = 0;
    while (s && s[i] && i + 1 < m) { d[i] = s[i]; i++; }
    d[i] = 0;
}

void _start(XToken ep_recv, XToken ep_ctl, XToken ser0) {
    (void)ep_ctl; (void)ser0;
    for (;;) {
        XToken page_tok = 0;
        if (xsys_accept(ep_recv, &page_tok) < 0 || !page_tok) { xsys_yield(); continue; }
        if (xsys_map(page_tok, IPC_MAP_VA, XPERM_RW) < 0) { xsys_yield(); continue; }
        XasmIpcMsg *msg = (XasmIpcMsg *)(uintptr_t)IPC_MAP_VA;
        XToken reply_ep = msg->reply_ep;
        msg->status = 0;
        if (msg->op == OP_AUX_SYSINFO) {
            scpy(msg->data, "PCIe, xHCI, USB-HID, USB-MSC, NVMe, AHCI, IDE, GOP, UART", sizeof(msg->data));
            msg->len = (uint32_t)u_strlen(msg->data);
        } else msg->status = -38;
        (void)xsys_unmap(IPC_MAP_VA);
        if (reply_ep) (void)xsys_offer(page_tok, reply_ep);
    }
}
