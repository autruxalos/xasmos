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
#include "exfs.h"
#include "dev.h"
#include "ramdisk.h"
#include "storage.h"
#include "xhci.h"
#include "usb_msc.h"
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
    if (!boot || exfs_format(boot) != EXFS_OK) {
        xk_println("[EXIT ERROR] boot volume format failed");
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
        if (sys && exfs_mount(sys) == EXFS_OK) {
            xk_println("[EXIT] root volume: |dev|nd0 (exfs, mounted)");
        } else if (exfs_mount(boot) == EXFS_OK) {
            xk_println("[EXIT] root volume: |dev|rd0 (no valid system disk)");
        } else {
            xk_println("[EXIT ERROR] no volume could be mounted");
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
        if (rd) xtok_root_mint(rd, boot_owner);
        if (nd) xtok_root_mint(nd, boot_owner);
        if (rd) g_exit_root_rd0 = boot_hand_root(rd, "rd0", boot_owner, init_owner);
        if (nd) g_exit_root_nd0 = boot_hand_root(nd, "nd0", boot_owner, init_owner);
        boot_hand_budget(boot_owner, init_owner);
        boot_shell_endpoint(init_owner);
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
extern void shell_set_caps(UINT32 owner, XToken bud, XToken rd0, XToken nd0);
extern int shell_dispatch(const char *cmd, const char *arg);
extern struct { int pending; char cmd[64]; char arg[200]; XToken bud, rd0, nd0; } g_shell_req;

void exit_launch_shell(void) {
    if (g_shell_owner == XTOK_OWNER_KERNEL) return;
    XToken load_bud = XTOKEN_INVALID;
    if (xtok_budget_mint(g_shell_owner, 256, &load_bud) != 0) return;
    XAspace as;
    if (xas_create(&as, g_shell_owner) != XAS_OK) { xtok_release(load_bud); return; }
    XLoadResult res;
    if (xload_exb(&as, g_shell_owner, (uint64_t)load_bud, xsh_prog_exb, xsh_prog_exb_len, &res) != XLOAD_OK) {
        xas_destroy(&as);
        xtok_release(load_bud);
        return;
    }
    XContext ctx;
    XProcResult r;
    xctx_init(&ctx, res.entry, res.stack_top);
    ctx.rdi = (UINT64)g_shell_recv[0];
    ctx.rsi = (UINT64)g_shell_recv[1];
    ctx.rdx = (UINT64)g_shell_recv[2];
    for (;;) {
        xproc_run_ctx(&as, &ctx, &r);
        if (r.reason == XPROC_YIELD && g_shell_req.pending) {
            g_shell_req.pending = 0;
            shell_set_caps(g_shell_owner, g_shell_req.bud, g_shell_req.rd0, g_shell_req.nd0);
            shell_dispatch(g_shell_req.cmd, g_shell_req.arg);
            continue;
        }
        break;
    }
    xas_destroy(&as);
    xtok_release(load_bud);
    owner_retire(g_shell_owner);
}
