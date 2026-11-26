#include "hda.h"
#include "serviceexfs_exb.h"
#include "serviceauxlad_exb.h"
#include "e1000.h"
#include "usb_hid.h"
/******************************************************************************
 * XASMOS - bootstrap stage: volumes
 * Path: source/arch/amd64/init/exit/stage_volumes.c
 *
 * Creates the boot volume (|dev|rd0) and publishes the system disk
 * (|dev|nd0) when one is present. Nothing here writes to a disk.
 *
 * Boot report: the boot volume's memory must lie inside the kernel image
 * range the allocator reserved. If it does not, the volume could be handed
 * out as page tables, so the stage fails.
 ******************************************************************************/
#include "exit_prog_exb.h"
#include "../../apps/xsh/images/xsh_prog_exb.h"
#include "capability.h"
#include "xload.h"
#include "xproc.h"
#include "paging.h"
#include "mem/owner.h"
#include "mem/budget.h"
#include "mem/endpoint.h"
#include "dev.h"
#include "ramdisk.h"
#include "storage.h"
#include "xhci.h"
#include "usb_msc.h"
#include "ahci.h"
#include "ide.h"
#include "stage_volumes.h"
#include "kmemory.h"
#include "pci.h"
#include "xring_table.h"
#include "mem/owner.h"
#include "xring.h"
#include "irqvec.h"
#include "lapic.h"
#include "paging.h"
#include "capability.h"

#define BOOT_VOLUME_BYTES (64u * 1024u * 1024u)

extern void xk_println(const char *s);
extern void xk_print(const char *s);
/* Hex printing for the boot report. shell's put_hex lives in xsh, which
 * is not linked into this stage, so the digits are written here. */
static void report_dec(unsigned v) {
    char b[12]; int i = 11; b[i] = '\0';
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v && i > 0);
    xk_print(&b[i]);
}

static void report_hex(UINT64 v, unsigned width) {
    static const char h[] = "0123456789ABCDEF";
    char b[17];
    unsigned i;
    for (i = 0; i < width; i++) {
        b[width - 1 - i] = h[v & 0xF];
        v >>= 4;
    }
    b[width] = '\0';
    xk_print(b);
}

static uint8_t g_boot_mem[BOOT_VOLUME_BYTES];
static RamDisk g_boot;

/* Reports where the volume lives and whether the reserved kernel image covers
 * it. Returns 1 if covered, 0 if not. */
static int boot_report(void) {
    UINT64 base = 0, size = 0;
    phys_image_range(&base, &size);

    UINT64 vol = (UINT64)(UINTN)g_boot_mem;
    UINT64 vol_end = vol + BOOT_VOLUME_BYTES;
    int covered = (size != 0 && vol >= base && vol_end <= base + size);

    xk_print("[EXIT] image reserved ");
    report_hex(base, 16);
    xk_print(" + ");
    report_hex(size, 8);
    xk_println("");

    xk_print("[EXIT] boot volume memory ");
    report_hex(vol, 16);
    xk_print(" + ");
    report_hex(BOOT_VOLUME_BYTES, 8);
    xk_println(covered ? " : inside the reserved image" : " : OUTSIDE the reserved image");
    return covered;
}

int nvme_irq_vector(void);

void nvme_route_unbind(uint32_t ring);
int  nvme_route_bind(uint32_t ring, void (*fn)(void *arg, uint32_t slot, int32_t st), void *arg);

/* Handles of the roots given to init, kept so the first program (exit) can be
 * started with them in its registers. The boot keeps no authority over them. */
XToken g_exit_root_rd0 = XTOKEN_INVALID;
XToken g_exit_root_nd0 = XTOKEN_INVALID;

/* Boot budget: the balance of memory the boot may hand out. The frames it names
 * are charged when spent (XSYS_ALLOC_FROM), not here. */
#define BOOT_BUDGET_FRAMES 16384u   /* 64 MiB, the boot volume's size */
XToken g_exit_budget = XTOKEN_INVALID;
/* Shell identity: a fixed, long-lived owner created by the boot. It is not
 * RUN_OWNER, so the reap of a program can never reach the shell's tokens. */
UINT32 g_shell_owner = 0;
/* Three lanes, one pending offer each (an endpoint holds one offer at a time). */
#define SHELL_LANE_BUDGET 0
#define SHELL_LANE_RD0    1
#define SHELL_LANE_ND0    2
XToken g_shell_recv[3] = { XTOKEN_INVALID, XTOKEN_INVALID, XTOKEN_INVALID };
static XToken g_shell_ser0 = XTOKEN_INVALID, g_shell_fb0 = XTOKEN_INVALID;
XToken g_shell_send[3] = { XTOKEN_INVALID, XTOKEN_INVALID, XTOKEN_INVALID };
XToken g_shell_budget  = XTOKEN_INVALID;   /* accepted by the boot in the shell's name */
XToken g_shell_rd0     = XTOKEN_INVALID;
XToken g_shell_nd0     = XTOKEN_INVALID;


/* S2a: the shell's endpoint. The receiver (shell) opens it and mints the SEND
 * for init_owner; no token moves, only the right to offer is issued. */
static void boot_shell_endpoint(UINT32 init_owner) {
    UINT32 i;
    g_shell_owner = owner_new();
    if (g_shell_owner == 0) { xk_println("[BOOT] shell owner FAILED"); return; }
    for (i = 0; i < 3; i++) {
        if (xep_create(g_shell_owner, &g_shell_recv[i]) != 0 ||
            xep_send_cap(g_shell_owner, g_shell_recv[i], init_owner, &g_shell_send[i]) != 0) {
            xk_println("[BOOT] shell endpoint FAILED");
            return;
        }
    }
    xk_println("[BOOT] shell endpoints ready (3)");
}

static void exit_print_u(UINT64 v);
static void boot_hand_budget(UINT32 boot_owner, UINT32 init_owner) {
    XToken b = XTOKEN_INVALID, got = XTOKEN_INVALID;
    if (xtok_budget_mint(boot_owner, BOOT_BUDGET_FRAMES, &b) != 0) {
        xk_println("[BOOT] budget mint FAILED");
        return;
    }
    if (xep_transfer(boot_owner, b, init_owner, &got) != 0) {
        xk_println("[BOOT] budget give FAILED");
        return;
    }
    g_exit_budget = got;
    {
        UINT64 bal = 0;
        if (xtok_budget_balance(got, init_owner, &bal) == 0) {
            xk_print("[BOOT] budget -> init, balance ");
            exit_print_u(bal);
            xk_println("");
        } else {
            xk_println("[BOOT] budget -> init, balance UNREADABLE");
        }
    }
}

/* Claims a disk root for the boot owner and hands it to init. The boot keeps
 * nothing: the give moves the token, it does not copy it. Returns the handle. */
static int ser0_read(void *ctx, uint64_t lba, uint32_t count, void *buf) { (void)ctx; (void)lba; (void)count; (void)buf; return BLK_OK; }
static int ser0_write(void *ctx, uint64_t lba, uint32_t count, const void *buf) {
    (void)ctx; (void)lba; const char *s = (const char *)buf;
    for (uint32_t i = 0; i < count; i++) { char c[2] = { s[i], 0 }; xk_print(c); }
    return BLK_OK;
}
static int ser0_flush(void *ctx) { (void)ctx; return BLK_OK; }
static const BlkOps g_ser0_ops = { ser0_read, ser0_write, ser0_flush, 0 };
static BlkDev g_ser0_dev = { 0, &g_ser0_ops, 1u, 1024ULL };
static const BlkOps g_fb0_ops = { ser0_read, ser0_write, ser0_flush, 0 };
static BlkDev g_fb0_dev = { 0, &g_fb0_ops, 4096u, 512ULL };

static XToken boot_hand_root(BlkDev *d, const char *name, UINT32 boot_owner, UINT32 init_owner) {
    XToken t = xtok_root_claim(d, boot_owner);
    xk_print("[BOOT] root ");
    xk_print(name);
    if (t == XTOKEN_INVALID) {
        xk_println(" claim FAILED");
        return XTOKEN_INVALID;
    }
    if (xtok_give(boot_owner, t, init_owner) != 0) {
        xk_println(" give FAILED");
        return XTOKEN_INVALID;
    }
    xk_println(" -> init");
    return t;
}

/* exit.exb: the first program. The boot always runs it, with init_owner and the
 * two disk roots in rdi and rsi. Its result is printed and the shell continues
 * whatever it returns. init_owner is the owner of the handoff, not a permanent
 * one: the roots go back to boot_owner, and init_owner is retired empty. */
#define EXIT_MAX_SLICES 1000

static void exit_print_u(UINT64 v) {
    char buf[21];
    int n = 0;
    do { buf[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < 20);
    while (n) { char c[2] = { buf[--n], 0 }; xk_print(c); }
}

/* Teardown. Not xproc_reap(init_owner): that would reclaim the roots themselves.
 * A root whose give fails stays with init_owner and is never reclaimed. */

static void exit_cleanup(UINT32 boot_owner, UINT32 init_owner) {
    XToken list[64];
    XToken keep[6];
    UINT32 nk = 0, n, k, i, progress;
    (void)boot_owner;
    keep[nk++] = g_exit_root_rd0;
    keep[nk++] = g_exit_root_nd0;
    keep[nk++] = g_exit_budget;
    for (i = 0; i < 3; i++) keep[nk++] = g_shell_send[i];
    for (;;) {
        n = xtok_owner_tokens(init_owner, list, 64);
        if (n == 0) break;
        progress = 0;
        for (k = 0; k < n; k++) {
            int kept = 0;
            for (i = 0; i < nk; i++) if (list[k] == keep[i]) kept = 1;
            if (kept) continue;
            xas_revoke_token(list[k]);
            if (xtok_reclaim(list[k]) == 0) progress++;
        }
        if (progress == 0) break;
    }
    {
        UINT64 f = 0;
        UINT32 t = 0;
        xtok_owner_usage(init_owner, &f, &t);
        xk_print("[EXIT] init owner kept: ");
        exit_print_u(t);
        xk_println(" token(s): budget remainder and the shell's SENDs");
    }
}

static void exit_launch(UINT32 boot_owner, UINT32 init_owner) {
    XAspace as;
    XLoadResult res;
    XContext ctx;
    XProcResult r;
    int lrc = XLOAD_OK;
    int st = XPROC_PREEMPT;
    unsigned slices = 0;

    r.reason = -1;
    r.exit_code = 0;

    if (xas_create(&as, init_owner) != XAS_OK) {
        xk_println("[EXIT] exit: no address space");
        exit_cleanup(boot_owner, init_owner);
        return;
    }
    lrc = xload_exb(&as, init_owner, (uint64_t)g_exit_budget, exit_prog_exb, exit_prog_exb_len, &res);
    if (lrc != XLOAD_OK) {
        xk_print("[EXIT] exit: load failed, code ");
        exit_print_u((UINT64)(-lrc));
        xk_println("");
    } else {
        xctx_init(&ctx, res.entry, res.stack_top);
        ctx.rdi = (UINT64)g_exit_root_rd0;
        ctx.rsi = (UINT64)g_exit_root_nd0;
        ctx.rdx = (UINT64)g_exit_budget;
        ctx.rcx = (UINT64)g_shell_send[SHELL_LANE_BUDGET];
        ctx.r8  = (UINT64)g_shell_send[SHELL_LANE_RD0];
        ctx.r9  = (UINT64)g_shell_send[SHELL_LANE_ND0];
        while ((st == XPROC_PREEMPT || st == XPROC_YIELD) && slices < EXIT_MAX_SLICES) {
            st = xproc_run_ctx(&as, &ctx, &r);
            slices++;
        }
        if (st == XPROC_EXIT) {
            xk_print("[EXIT] exit: code ");
            exit_print_u(r.exit_code);
            xk_println("");
        } else {
            xk_print("[EXIT] exit: did not finish, reason ");
            exit_print_u((UINT64)r.reason);
            xk_print(" slices ");
            exit_print_u(slices);
            xk_println("");
        }
        xas_destroy(&as);
    }
    exit_cleanup(boot_owner, init_owner);
}

int stage_volumes(void) {
    if (ramdisk_init(&g_boot, g_boot_mem, BOOT_VOLUME_BYTES, 512u) != BLK_OK) {
        xk_println("[EXIT ERROR] boot volume memory rejected");
        return -1;
    }
    if (!boot_report()) {
        xk_println("[EXIT ERROR] boot volume is not covered by the reserved image");
        return -1;
    }
    if (dev_register("rd0", ramdisk_device(&g_boot)) != DEV_OK) {
        xk_println("[EXIT ERROR] cannot publish |dev|rd0");
        return -1;
    }
    BlkDev *boot = dev_lookup("|dev|rd0");
    if (!boot) {
        xk_println("[EXIT ERROR] boot volume missing");
        return -1;
    }
    xk_println("[EXIT] boot volume |dev|rd0: exfs, 64 MiB, RAM");

    if (storage_open() == STORAGE_OK && storage_device() != 0) {
        if (dev_register("nd0", storage_device()) == DEV_OK) {
            char serial[DEV_SERIAL_MAX + 1];
            if (storage_serial(serial, sizeof serial)) {
                dev_set_serial("nd0", serial);
            }
            xk_println("[EXIT] system disk |dev|nd0 registered");
        }
    } else {
        xk_println("[EXIT] no system disk: |dev|nd0 unavailable");
    }
    {
        const PciDev *nv = pci_find(0x01, 0x08, PCI_ANY_PROGIF, 0);
        PciMsix mx;
        int rc = nv ? pci_msix_info(nv, &mx) : -3;
        if (rc == 0) {
            xk_print("[PCI] nvme msix: ");
            report_dec(mx.entries);
            xk_print(" entries, BAR ");
            report_dec(mx.bir);
            xk_print(" +");
            report_hex(mx.table_off, 8);
            xk_println("");
        } else {
            xk_print("[PCI] nvme: no MSI-X, rc=-");
            report_dec((unsigned)(-rc));
            xk_println(rc == -3 ? " (no NVMe found)" : (rc == -1 ? " (no capability)" : " (table BAR not memory)"));
        }
    }

    /* Root volume: the system disk if it holds a valid EXFS volume, otherwise
     * the boot RAM volume. Mounting only reads; nothing is written here. */
    {
        BlkDev *sys = dev_lookup("|dev|nd0");
        if (sys) {
            xk_println("[EXIT] root block device: |dev|nd0 (handed to init)");
        } else {
            xk_println("[EXIT] root block device: |dev|rd0 (handed to init)");
        }
    }

    /* Boot roots: the kernel mints one root per disk, the boot owner claims it,
     * and init receives it. The pair boot -> init is policy; it moves to
     * init.exb once the boot is a program. */
    {
        UINT32  boot_owner = owner_new();
        UINT32  init_owner = owner_new();
        BlkDev *rd = dev_lookup("|dev|rd0");
        BlkDev *nd = dev_lookup("|dev|nd0");
        xtok_roots_reset();
        dev_register("|dev|ser0", &g_ser0_dev);
        dev_register("|dev|fb0",  &g_fb0_dev);
        xtok_con_register_devs(&g_ser0_dev, &g_fb0_dev);
        if (rd) xtok_root_mint(rd, boot_owner);
        if (nd) xtok_root_mint(nd, boot_owner);
        xtok_root_mint(&g_ser0_dev, boot_owner);
        xtok_root_mint(&g_fb0_dev,  boot_owner);
        if (rd) g_exit_root_rd0 = boot_hand_root(rd, "rd0", boot_owner, init_owner);
        if (nd) g_exit_root_nd0 = boot_hand_root(nd, "nd0", boot_owner, init_owner);
        XToken init_ser0 = boot_hand_root(&g_ser0_dev, "ser0", boot_owner, init_owner);
        XToken init_fb0  = boot_hand_root(&g_fb0_dev,  "fb0",  boot_owner, init_owner);
        boot_hand_budget(boot_owner, init_owner);
        boot_shell_endpoint(init_owner);
        XToken sh_ser0 = (init_ser0 != XTOKEN_INVALID) ? xtok_derive_blocks(init_owner, init_ser0, 0ULL, 1024ULL, XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_GIVE) : XTOKEN_INVALID;
        if (sh_ser0 != XTOKEN_INVALID) xtok_give(init_owner, sh_ser0, g_shell_owner);
        XToken sh_fb0  = (init_fb0 != XTOKEN_INVALID) ? xtok_derive_blocks(init_owner, init_fb0, 0ULL, 512ULL, XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | XTOK_RIGHT_GIVE) : XTOKEN_INVALID;
        if (sh_fb0 != XTOKEN_INVALID) xtok_give(init_owner, sh_fb0, g_shell_owner);
        g_shell_ser0 = sh_ser0; g_shell_fb0 = sh_fb0;
        xtok_con_bind_session(g_shell_owner, sh_ser0, sh_fb0);
        exit_launch(boot_owner, init_owner);
    }

    {
        XhciInfo xi;
        int xrc = xhci_probe_and_init(&xi);
        if (xrc == XHCI_OK) {
            xk_print("[XHCI] v");
            report_hex((UINT64)xi.hci_version, 4);
            xk_print(" slots="); report_hex((UINT64)xi.max_slots, 2);
            xk_print(" ports="); report_hex((UINT64)xi.max_ports, 2);
            xk_print(" ctx=");   report_hex((UINT64)xi.ctx_size, 2);
            xk_print("B conn="); report_hex((UINT64)xi.connected_ports, 2);
            UsbMscInfo mi;
            UsbHidInfo hi; int hrc = usb_hid_init(&hi); if (hrc == 0) { xk_print("[USB-HID] kbd="); report_hex((UINT64)hi.kbd_active, 2); xk_print(" mouse="); report_hex((UINT64)hi.mouse_active, 2); xk_println(""); }
    E1000Info ni; if (e1000_init(&ni) == 0) { xk_print("[NIC-E1000] dev="); report_hex((UINT64)ni.device, 4); xk_println(""); }
    HdaInfo ai; if (hda_init(&ai) == 0) { xk_print("[AUDIO-HDA] codecs="); report_hex((UINT64)ai.codec_mask, 4); xk_println(""); }
    int mrc = usb_msc_init(&mi);
            __asm__ volatile("sti; nop; nop; cli"); UINT32 xir = (xhci_irq_vector() >= 0) ? irq_vector_take((UINT32)xhci_irq_vector()) : 0u;
            xk_print(" irq="); report_hex((UINT64)xir, 2);
            xk_println(" noop=ok");
            if (mrc == 0) {
                uint8_t sec0[512];
                int rrc = usb_msc_read_blocks(0ULL, 1u, sec0);
                xk_print("[USB-MSC] slot="); report_hex((UINT64)mi.slot, 2);
                xk_print(" blocks="); report_hex(mi.blocks, 4);
                xk_print(" bsz="); report_hex((UINT64)mi.block_size, 4);
                if (rrc == 0) {
                    xk_print(" sig="); char sig[5] = {(char)sec0[0], (char)sec0[1], (char)sec0[2], (char)sec0[3], 0}; xk_print(sig);
                }
                xk_println("");
            } else {
                xk_print("[USB-MSC] init rc="); report_hex((UINT64)(UINT32)(-mrc), 2); xk_println("");
            }
        } else {
            xk_print("[XHCI] init rc="); report_hex((UINT64)(UINT32)(-xrc), 2); xk_println("");
        }
    }
    {
        AhciInfo ai;
        int arc = ahci_probe_and_init(&ai);
        if (arc == 0) {
            uint8_t s0[512];
            int rrc = blk_read(storage_ahci_device(), 0ULL, 1u, s0);
            xk_print("[AHCI] port="); report_hex((UINT64)ai.port, 2);
            xk_print(" sectors="); report_hex(ai.sectors, 4);
            xk_print(" model="); xk_print(ai.model);
            if (rrc == BLK_OK) { char sig[5] = {(char)s0[0],(char)s0[1],(char)s0[2],(char)s0[3],0}; xk_print(" sig="); xk_print(sig); }
            xk_println("");
        } else {
            xk_print("[AHCI] init rc="); report_hex((UINT64)(UINT32)(-arc), 2); xk_println("");
        }
    }
    {
        IdeInfo ii;
        int irc = ide_probe_and_init(&ii);
        if (irc == 0) {
            uint8_t s0[512];
            int rrc = blk_read(storage_ide_device(), 0ULL, 1u, s0);
            xk_print("[IDE] base="); report_hex((UINT64)ii.cmd_base, 4);
            xk_print(" sectors="); report_hex(ii.sectors, 4);
            xk_print(" model="); xk_print(ii.model);
            if (rrc == BLK_OK) { char sig[5] = {(char)s0[0],(char)s0[1],(char)s0[2],(char)s0[3],0}; xk_print(" sig="); xk_print(sig); }
            xk_println("");
        } else {
            xk_print("[IDE] init rc="); report_hex((UINT64)(UINT32)(-irc), 2); xk_println("");
        }
    }
    /* Program ring table: the boot sets the policy, programs never do.
     * Empty at boot, so this changes nothing until XSYS_QUEUE_CREATE exists. */
    xrt_init(nvme_route_unbind, nvme_route_bind);

    /* Delivery test: a polled read on nd0, then a controlled window with
     * interrupts on. The completion's MSI-X notice must be counted once. */
    if (storage_device() != 0 && nvme_irq_vector() >= 0) {
        uint8_t sec[512];
        int rc = blk_read(storage_device(), 0, 1, sec);
        __asm__ volatile ("sti; nop; nop; nop; cli");
        UINT32 n = irq_vector_take((UINT32)nvme_irq_vector());
        xk_print("[IRQ] nvme vector ");
        report_hex((UINT64)nvme_irq_vector(), 2);
        xk_print(rc == BLK_OK ? " read ok, notices=" : " read FAILED, notices=");
        report_dec(n);
        xk_println("");
    }
    {
        uint8_t b0 = 0;
        int st = kr_boot_selftest_nvme(&b0);
        if (st == 0) {
            xk_print("[RING] nvme read via ring: ok, block0 byte=");
            report_hex(b0, 2);
            xk_println("");
        } else {
            xk_print("[RING] nvme read via ring: FAILED, code -");
            report_dec((unsigned)(-st));
            xk_println("");
        }
    }

    return 0;
}
void exit_launch_shell(void) {
    if (g_shell_owner == XTOK_OWNER_KERNEL) return;
    XToken load_bud = XTOKEN_INVALID;
    if (xtok_budget_mint(g_shell_owner, 256, &load_bud) != 0) return;
    XAspace as;
    if (xas_create(&as, g_shell_owner) != XAS_OK) { xtok_release(load_bud); return; }
    XLoadResult res;
    int lrc = xload_exb(&as, g_shell_owner, (uint64_t)load_bud, xsh_prog_exb, xsh_prog_exb_len, &res); if (lrc != XLOAD_OK) { xk_print("[XSH load err="); report_dec((unsigned)(-lrc)); xk_println("]");
        xas_destroy(&as);
        xtok_release(load_bud);
        return;
    }
    UINT32 own_exfs = owner_new(), own_aux = owner_new();
    XToken bud_exfs = XTOKEN_INVALID, bud_aux = XTOKEN_INVALID;
    XToken ep_exfs_r = XTOKEN_INVALID, ep_exfs_s = XTOKEN_INVALID;
    XToken ep_aux_r = XTOKEN_INVALID, ep_aux_s = XTOKEN_INVALID;
    XToken ep_rep_r = XTOKEN_INVALID, ep_rep_s = XTOKEN_INVALID;
    XAspace as_exfs, as_aux; XLoadResult res_exfs, res_aux;
    XContext ctx_exfs, ctx_aux; XProcResult r_srv;
    int exfs_ok = 0, aux_ok = 0;
    if (own_exfs && xtok_budget_mint(own_exfs, 256, &bud_exfs) == 0 &&
        xep_create(own_exfs, &ep_exfs_r) == 0 &&
        xep_send_cap(own_exfs, ep_exfs_r, g_shell_owner, &ep_exfs_s) == 0 &&
        xas_create(&as_exfs, own_exfs) == XAS_OK &&
        xload_exb(&as_exfs, own_exfs, (uint64_t)bud_exfs, g_serviceexfs_exb, g_serviceexfs_exb_len, &res_exfs) == XLOAD_OK) {
        xctx_init(&ctx_exfs, res_exfs.entry, res_exfs.stack_top);
        ctx_exfs.rdi = (UINT64)ep_exfs_r;
        exfs_ok = 1;
        xk_println("[SERVICE] serviceexfs.exb armed (ring-3 zero-copy IPC)");
    }
    if (own_aux && xtok_budget_mint(own_aux, 256, &bud_aux) == 0 &&
        xep_create(own_aux, &ep_aux_r) == 0 &&
        xep_send_cap(own_aux, ep_aux_r, g_shell_owner, &ep_aux_s) == 0 &&
        xas_create(&as_aux, own_aux) == XAS_OK &&
        xload_exb(&as_aux, own_aux, (uint64_t)bud_aux, g_serviceauxlad_exb, g_serviceauxlad_exb_len, &res_aux) == XLOAD_OK) {
        xctx_init(&ctx_aux, res_aux.entry, res_aux.stack_top);
        ctx_aux.rdi = (UINT64)ep_aux_r;
        aux_ok = 1;
        xk_println("[SERVICE] serviceauxlad.exb armed (ring-3 zero-copy IPC)");
    }
    (void)xep_create(g_shell_owner, &ep_rep_r);
    (void)xep_send_cap(g_shell_owner, ep_rep_r, g_shell_owner, &ep_rep_s);
    XContext ctx; XProcResult r;
    xctx_init(&ctx, res.entry, res.stack_top);
    ctx.rdi = (UINT64)ep_exfs_s;
    ctx.rsi = (UINT64)ep_aux_s;
    ctx.rdx = (UINT64)ep_rep_r;
    ctx.rcx = (UINT64)g_shell_ser0;
    ctx.r8  = (UINT64)g_shell_fb0;
    ctx.r9  = (UINT64)ep_rep_s;
    for (;;) {
        if (exfs_ok) xproc_run_ctx(&as_exfs, &ctx_exfs, &r_srv);
        if (aux_ok)  xproc_run_ctx(&as_aux, &ctx_aux, &r_srv);
        xproc_run_ctx(&as, &ctx, &r);
        if (r.reason == XPROC_YIELD) continue;
        break;
    }
    xas_destroy(&as);
    xtok_release(load_bud);
    owner_retire(g_shell_owner);
}
