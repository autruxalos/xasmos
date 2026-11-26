#include "xasm_user.h"
#include "xasm_ipc_proto.h"
#define MAX_LOCAL_NODES 24
#define IPC_MAP_VA XASM_USER_IPC_VA
typedef struct { int used, is_dir; char name[48]; char data[176]; } LocalNode;
static XToken g_ep_exfs = 0, g_ep_auxlad = 0, g_ep_reply_recv = 0, g_ep_reply_send = 0, g_ser0 = 0, g_fb0 = 0;
static int g_is_sprusr = 0;
static char g_cwd[128] = "|user|xent";
static LocalNode g_fs[MAX_LOCAL_NODES];
static void out_s(const char *s) { if (g_ser0) u_print(g_ser0, s); if (g_fb0) u_print(g_fb0, s); }
static void out_ln(const char *s) { out_s(s); out_s("\n"); }
static void out_dec(uint64_t v) { char b[24]; int i = 0; if (!v) { out_s("0"); return; } while (v && i < 23) { b[i++] = (char)('0' + (v % 10u)); v /= 10u; } while (i > 0) { char c[2] = { b[--i], 0 }; out_s(c); } }
static int starts_with(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
static const char *skip_sp(const char *s) { while (*s == ' ') s++; return s; }
static void str_copy(char *d, const char *s, uint64_t m) { uint64_t i = 0; while (s && s[i] && i + 1 < m) { d[i] = s[i]; i++; } d[i] = 0; }
static void local_fs_init(void) { for (int i = 0; i < MAX_LOCAL_NODES; i++) g_fs[i].used = 0; g_fs[0].used = 1; g_fs[0].is_dir = 1; str_copy(g_fs[0].name, "|dev|rd0", 48); g_fs[1].used = 1; g_fs[1].is_dir = 1; str_copy(g_fs[1].name, "|dev|nd0", 48); }
static int local_find(const char *nm) { for (int i = 0; i < MAX_LOCAL_NODES; i++) if (g_fs[i].used && u_strcmp(g_fs[i].name, nm) == 0) return i; return -1; }
static int ipc_call(XToken target_ep, XasmIpcMsg *req) {
    if (!target_ep || !g_ep_reply_recv || !g_ep_reply_send) return -1;
    XToken pg = 0;
    if (xsys_alloc(1u, XTOK_RIGHT_READ|XTOK_RIGHT_WRITE|XTOK_RIGHT_GIVE|XTOK_RIGHT_MAP, &pg) < 0 || !pg) return -1;
    if (xsys_map(pg, IPC_MAP_VA, XPERM_RW) < 0) return -1;
    XasmIpcMsg *m = (XasmIpcMsg *)(uintptr_t)IPC_MAP_VA; *m = *req; m->reply_ep = g_ep_reply_send;
    (void)xsys_unmap(IPC_MAP_VA);
    if (xsys_offer(pg, target_ep) < 0) return -1;
    for (int sp = 0; sp < 2000; sp++) {
        XToken bk = 0;
        if (xsys_accept(g_ep_reply_recv, &bk) == 0 && bk) {
            if (xsys_map(bk, IPC_MAP_VA, XPERM_RW) == 0) { *req = *(XasmIpcMsg *)(uintptr_t)IPC_MAP_VA; (void)xsys_unmap(IPC_MAP_VA); return 0; }
        }
        xsys_yield();
    }
    return -1;
}
static void print_prompt(void) { if (u_strcmp(g_cwd, "|user|xent") == 0) out_s(g_is_sprusr ? "|user|sprusr|% " : "|user|xent|@ "); else { out_s(g_cwd); out_s(g_is_sprusr ? "|% " : "|@ "); } }
static void cmd_locate(const char *a) { a = skip_sp(a); if (!*a) { str_copy(g_cwd, "|user|xent", 128); return; } if (a[0] == '|') { str_copy(g_cwd, a, 128); return; } uint64_t n = u_strlen(g_cwd); if (n + 1 + u_strlen(a) < 128) { if (n > 1) g_cwd[n++] = '|'; str_copy(g_cwd + n, a, 128 - n); } }
void _start(XToken ep_exfs, XToken ep_auxlad, XToken ep_reply_recv, XToken ser0, XToken fb0, XToken ep_reply_send) {
    g_ep_exfs = ep_exfs; g_ep_auxlad = ep_auxlad; g_ep_reply_recv = ep_reply_recv; g_ep_reply_send = ep_reply_send;
    g_ser0 = ser0; g_fb0 = fb0; XToken con = g_ser0 ? g_ser0 : g_fb0; local_fs_init();
    out_ln("XASMOS Shell v0.08 (xeret ring-3, zero kernel policy) -- ver lists directives");
    char line[256];
    for (;;) {
        print_prompt(); int64_t n = xsys_readline(con, line, 256); if (n <= 0) { xsys_yield(); continue; }
        const char *cmd = skip_sp(line); if (!*cmd) continue;
        if (u_strcmp(cmd, "ver") == 0) out_ln("Directives: ver, clear, list, make -dir|-file, remove, read, write, locate, whereami, sprusr, sysinfo, exofetch, mem, tokens, format, mount, halt");
        else if (u_strcmp(cmd, "clear") == 0) { if (g_ser0) xsys_clear(g_ser0); if (g_fb0) xsys_clear(g_fb0); }
        else if (u_strcmp(cmd, "whereami") == 0 || u_strcmp(cmd, "pwd") == 0) out_ln(g_cwd);
        else if (starts_with(cmd, "locate ")) cmd_locate(cmd + 6);
        else if (starts_with(cmd, "cd ")) cmd_locate(cmd + 2);
        else if (u_strcmp(cmd, "sprusr") == 0 || u_strcmp(cmd, "su") == 0) { g_is_sprusr = !g_is_sprusr; out_ln(g_is_sprusr ? "[AUTH] |user|sprusr|%" : "[AUTH] |user|xent|@"); }
        else if (u_strcmp(cmd, "sysinfo") == 0 || u_strcmp(cmd, "exofetch") == 0) {
            XasmIpcMsg m; u_memset(&m, 0, sizeof(m)); m.op = OP_AUX_SYSINFO;
            const char *dv = (ipc_call(g_ep_auxlad, &m) == 0 && m.status == 0) ? m.data : "PCIe, xHCI, USB-HID, USB-MSC, NVMe, AHCI, IDE, GOP, UART";
            out_ln("XASMOS x86_64 (xeret ring-3 LibOS) | Shell: XSH v0.08 (zero-copy IPC)");
            out_s("Frames used/free: "); out_dec((uint64_t)xsys_info(0)); out_s("/"); out_dec((uint64_t)xsys_info(1)); out_ln("");
            out_s("Devices: "); out_ln(dv);
        } else if (u_strcmp(cmd, "list") == 0) {
            XasmIpcMsg m; u_memset(&m, 0, sizeof(m)); m.op = OP_EXFS_LIST;
            if (ipc_call(g_ep_exfs, &m) == 0 && m.status == 0) { out_s("[EXFS-IPC] "); out_ln(m.data); }
            else { out_s("[EXFS] "); for (int i = 0; i < MAX_LOCAL_NODES; i++) if (g_fs[i].used) { out_s(g_fs[i].name); out_s("  "); } out_ln(""); }
        } else if (starts_with(cmd, "make ")) {
            const char *a = skip_sp(cmd + 5); int d = starts_with(a, "-dir "); if (d) a = skip_sp(a + 5); else if (starts_with(a, "-file ")) a = skip_sp(a + 6);
            XasmIpcMsg m; u_memset(&m, 0, sizeof(m)); m.op = OP_EXFS_MAKE; m.flags = (uint32_t)d; str_copy(m.name, a, 48);
            if (ipc_call(g_ep_exfs, &m) == 0 && m.status == 0) { out_s("[EXFS-IPC] created "); out_ln(a); }
            else if (local_find(a) < 0) { for (int i = 0; i < MAX_LOCAL_NODES; i++) if (!g_fs[i].used) { g_fs[i].used = 1; g_fs[i].is_dir = d; str_copy(g_fs[i].name, a, 48); break; } out_s("[EXFS] created "); out_ln(a); }
        } else if (starts_with(cmd, "write ")) {
            const char *p = skip_sp(cmd + 6); char nm[48]; int k = 0; while (*p && *p != ' ' && k < 47) nm[k++] = *p++; nm[k] = 0; p = skip_sp(p);
            XasmIpcMsg m; u_memset(&m, 0, sizeof(m)); m.op = OP_EXFS_WRITE; str_copy(m.name, nm, 48); str_copy(m.data, p, 176);
            if (ipc_call(g_ep_exfs, &m) == 0 && m.status == 0) { out_s("[EXFS-IPC] wrote "); out_ln(nm); }
            else { int idx = local_find(nm); if (idx >= 0) { str_copy(g_fs[idx].data, p, 176); out_s("[EXFS] wrote "); out_ln(nm); } else out_ln("[EXFS] not found"); }
        } else if (starts_with(cmd, "read ")) {
            const char *a = skip_sp(cmd + 5); XasmIpcMsg m; u_memset(&m, 0, sizeof(m)); m.op = OP_EXFS_READ; str_copy(m.name, a, 48);
            if (ipc_call(g_ep_exfs, &m) == 0 && m.status == 0) out_ln(m.data);
            else { int idx = local_find(a); if (idx >= 0) out_ln(g_fs[idx].data); else out_ln("[EXFS] not found"); }
        } else if (starts_with(cmd, "remove ")) {
            const char *a = skip_sp(cmd + 7); XasmIpcMsg m; u_memset(&m, 0, sizeof(m)); m.op = OP_EXFS_REMOVE; str_copy(m.name, a, 48);
            if (ipc_call(g_ep_exfs, &m) == 0 && m.status == 0) { out_s("[EXFS-IPC] removed "); out_ln(a); }
            else { int idx = local_find(a); if (idx >= 0) { g_fs[idx].used = 0; out_s("[EXFS] removed "); out_ln(a); } else out_ln("[EXFS] not found"); }
        } else if (u_strcmp(cmd, "format") == 0 || u_strcmp(cmd, "mount") == 0) {
            if (!g_is_sprusr) out_ln("[ERR_PERM] sprusr required"); else { if (u_strcmp(cmd, "format") == 0) local_fs_init(); out_s("[EXFS] "); out_s(cmd); out_ln(" ok"); }
        } else if (starts_with(cmd, "echo ")) out_ln(cmd + 5);
        else if (u_strcmp(cmd, "halt") == 0) xsys_exit(0);
        else { out_s("[ERR] "); out_ln(cmd); }
    }
}
