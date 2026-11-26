#include "xasm_user.h"
#include "xasm_ipc_proto.h"

#define MAX_EXFS_NODES 32
#define IPC_MAP_VA     0x0000008000040000ULL

typedef struct { int used, is_dir; char name[48]; char data[176]; } ExfsNode;
static ExfsNode g_nodes[MAX_EXFS_NODES];

static void scpy(char *d, const char *s, uint64_t m) {
    uint64_t i = 0;
    while (s && s[i] && i + 1 < m) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void init_fs(void) {
    for (int i = 0; i < MAX_EXFS_NODES; i++) g_nodes[i].used = 0;
    g_nodes[0].used = 1; g_nodes[0].is_dir = 1; scpy(g_nodes[0].name, "|dev|rd0", 48);
    g_nodes[1].used = 1; g_nodes[1].is_dir = 1; scpy(g_nodes[1].name, "|dev|nd0", 48);
    g_nodes[2].used = 1; g_nodes[2].is_dir = 0; scpy(g_nodes[2].name, "serviceexfs.exb", 48);
    g_nodes[3].used = 1; g_nodes[3].is_dir = 0; scpy(g_nodes[3].name, "serviceauxlad.exb", 48);
}

static int find_node(const char *name) {
    for (int i = 0; i < MAX_EXFS_NODES; i++)
        if (g_nodes[i].used && u_strcmp(g_nodes[i].name, name) == 0) return i;
    return -1;
}

void _start(XToken ep_recv, XToken blk_rd0, XToken ser0) {
    (void)blk_rd0; (void)ser0;
    init_fs();
    for (;;) {
        XToken page_tok = 0;
        if (xsys_accept(ep_recv, &page_tok) < 0 || !page_tok) { xsys_yield(); continue; }
        if (xsys_map(page_tok, IPC_MAP_VA, XPERM_RW) < 0) { xsys_yield(); continue; }
        XasmIpcMsg *msg = (XasmIpcMsg *)(uintptr_t)IPC_MAP_VA;
        XToken reply_ep = msg->reply_ep;
        msg->status = 0;
        if (msg->op == OP_EXFS_LIST) {
            uint64_t p = 0; msg->data[0] = 0;
            for (int i = 0; i < MAX_EXFS_NODES; i++) {
                if (!g_nodes[i].used) continue;
                uint64_t nl = u_strlen(g_nodes[i].name);
                if (p + nl + 3 < sizeof(msg->data)) {
                    if (p > 0) { msg->data[p++] = ' '; msg->data[p++] = ' '; }
                    scpy(msg->data + p, g_nodes[i].name, sizeof(msg->data) - p);
                    p += nl;
                }
            }
            msg->len = (uint32_t)p;
        } else if (msg->op == OP_EXFS_MAKE) {
            if (find_node(msg->name) >= 0) msg->status = -17;
            else {
                int slot = -1;
                for (int i = 0; i < MAX_EXFS_NODES; i++) if (!g_nodes[i].used) { slot = i; break; }
                if (slot < 0) msg->status = -28;
                else { g_nodes[slot].used = 1; g_nodes[slot].is_dir = (int)(msg->flags & 1u); scpy(g_nodes[slot].name, msg->name, 48); g_nodes[slot].data[0] = 0; }
            }
        } else if (msg->op == OP_EXFS_REMOVE) {
            int idx = find_node(msg->name);
            if (idx < 0) msg->status = -2; else g_nodes[idx].used = 0;
        } else if (msg->op == OP_EXFS_WRITE) {
            int idx = find_node(msg->name);
            if (idx < 0) msg->status = -2; else scpy(g_nodes[idx].data, msg->data, sizeof(g_nodes[idx].data));
        } else if (msg->op == OP_EXFS_READ) {
            int idx = find_node(msg->name);
            if (idx < 0) msg->status = -2; else scpy(msg->data, g_nodes[idx].data, sizeof(msg->data));
        } else if (msg->op == OP_EXFS_FORMAT || msg->op == OP_EXFS_MOUNT) {
            if (!(msg->flags & 2u)) msg->status = -1;
            else if (msg->op == OP_EXFS_FORMAT) init_fs();
        } else msg->status = -38;
        (void)xsys_unmap(IPC_MAP_VA);
        if (reply_ep) (void)xsys_offer(page_tok, reply_ep);
    }
}
