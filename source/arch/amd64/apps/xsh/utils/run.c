/******************************************************************************
* XASMOS - Coreutil: run. Loads an embedded .exb image and runs it.
* Path: source/arch/amd64/apps/xsh/utils/run.c
*
* Every run is an instance with its own owner. The shell lends it a child of its
* budget (rdx) and, if the program is listed, a child of one disk root (rcx).
* Nothing is named inside the program: the disk comes as a token. The reap gives
* every token back and the owner retires empty.
******************************************************************************/
#include "common.h"
#include "../images/images.h"
#include <xload.h>
#include <xproc.h>
#include <capability.h>
#include "mem/owner.h"
#include "mem/budget.h"
#include "mem/endpoint.h"

#define RUN_SLICE_MS     10u
#define RUN_MAX_SLICES   500u
#define RUN_CHILD_FRAMES 1024ull
#define RUN_LANE_RIGHTS  (XTOK_RIGHT_READ | XTOK_RIGHT_RELEASE | XTOK_RIGHT_GIVE)

/* Shell identity and what the boot accepted for it. The shell is still in
 * BOOTX64.EFI: declared deuda until it is a .exb. */
extern UINT32 g_shell_owner;
extern XToken g_shell_budget;
extern XToken g_shell_rd0;
extern XToken g_shell_nd0;

/* Shell policy: which program is lent which disk lane. A program not listed
 * gets no disk at all. */
#define LANE_RD0 0
#define LANE_ND0 1
typedef struct { const char *name; int lane; UINT64 first, count; } RunDisk;
static const RunDisk g_disk_for[] = {
    { "queue_prog",  LANE_RD0,    0ull, 64ull },
    { "derive_prog", LANE_RD0,    0ull, 64ull },
    { "nd0_prog",    LANE_ND0, 4096ull,  8ull },
};

static int same_name(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void list_images(void) {
   int n = xsh_image_count();
   xk_println("embedded images:");
   for (int i = 0; i < n; i++) {
       xk_print("  ");
       xk_println(xsh_image_at(i)->name);
   }
}

static XToken lend_budget(UINT32 owner) {
   XToken child = XTOKEN_INVALID, got = XTOKEN_INVALID;
   if (g_shell_budget == XTOKEN_INVALID) {
       xk_println("[RUN] no shell budget: the program gets none");
       return XTOKEN_INVALID;
   }
   if (xtok_budget_derive(g_shell_owner, g_shell_budget, RUN_CHILD_FRAMES, &child) != 0) {
       xk_println("[RUN] shell budget too small: the program gets none");
       return XTOKEN_INVALID;
   }
   if (xep_transfer(g_shell_owner, child, owner, &got) != 0) {
       xtok_release_owned(child, g_shell_owner);
       xk_println("[RUN] budget transfer FAILED");
       return XTOKEN_INVALID;
   }
   return got;
}

static XToken lend_disk(UINT32 owner, const char *name) {
   const RunDisk *d = 0;
   XToken root, child, got = XTOKEN_INVALID;
   for (unsigned i = 0; i < sizeof g_disk_for / sizeof g_disk_for[0]; i++)
       if (same_name(g_disk_for[i].name, name)) { d = &g_disk_for[i]; break; }
   if (!d) return XTOKEN_INVALID;

   root = (d->lane == LANE_ND0) ? g_shell_nd0 : g_shell_rd0;
   if (root == XTOKEN_INVALID) {
       xk_println("[RUN] shell holds no disk root: the program gets no disk");
       return XTOKEN_INVALID;
   }
   child = xtok_derive_blocks(g_shell_owner, root, d->first, d->count, RUN_LANE_RIGHTS);
   if (child == XTOKEN_INVALID) {
       xk_println("[RUN] disk lane refused by the root");
       return XTOKEN_INVALID;
   }
   if (xep_transfer(g_shell_owner, child, owner, &got) != 0) {
       xtok_release_owned(child, g_shell_owner);
       xk_println("[RUN] disk transfer FAILED");
       return XTOKEN_INVALID;
   }
   return got;
}

static void report_shell_budget(void) {
   UINT64 bal = 0;
   if (g_shell_budget != XTOKEN_INVALID &&
       xtok_budget_balance(g_shell_budget, g_shell_owner, &bal) == 0) {
       xk_print("[RUN] shell budget: ");
       put_dec(bal);
       xk_println("");
   }
}

int cmd_run_main(const char *name) {
   if (name[0] == '\0') {
       list_images();
       return 0;
   }

   const XshImage *img = xsh_image_find(name);
   if (!img) {
       xk_print("EXNOENT: no embedded image named ");
       xk_println(name);
       return -1;
   }

   UINT32 owner = owner_new();
   if (owner == 0) {
       xk_println("EXNOMEM: no owner id free");
       return -1;
   }

   XAspace as;
   if (xas_create(&as, owner) != XAS_OK) {
       xk_println("EXNOMEM: no address space for the program");
       owner_retire(owner);
       return -1;
   }

   XLoadResult res;
   int rc = xload_exb(&as, owner, img->data, img->len, &res);
   if (rc != XLOAD_OK) {
       xk_print("EXBADIMAGE: load failed, code ");
       put_dec((unsigned long long)(-rc));
       xk_println("");
       xas_destroy(&as);
       owner_retire(owner);
       return -1;
   }

   XContext ctx;
   XProcResult r;
   xctx_init(&ctx, res.entry, res.stack_top);
   ctx.rdx = (UINT64)lend_budget(owner);
   ctx.rcx = (UINT64)lend_disk(owner, name);

   if (xproc_set_quantum_ms(RUN_SLICE_MS) != 0) {
       xproc_set_quantum_ms(0);
   }

   int st = XPROC_PREEMPT;
   unsigned slices = 0;
   while ((st == XPROC_PREEMPT || st == XPROC_YIELD) && slices < RUN_MAX_SLICES) {
       st = xproc_run_ctx(&as, &ctx, &r);
       slices++;
   }
   xproc_set_quantum_ms(0);

   if (st == XPROC_PREEMPT) {
       xk_println("EXTIMEOUT: program used its CPU limit and was stopped");
   } else if (st == XPROC_EXIT) {
       xk_print("exit=");
       put_dec(r.exit_code);
       xk_println("");
   } else if (st == XPROC_FAULT) {
       xk_print("EXFAULT: vector=");
       put_dec(r.vector);
       xk_print(" error=");
       put_hex(r.error, 4);
       xk_print(" cr2=");
       put_hex(r.cr2, 16);
       xk_print(" rip=");
       put_hex(r.rip, 16);
       xk_println("");
   } else {
       xk_print("EXSTATE: unexpected run state reason=");
       put_dec((unsigned long long)r.reason);
       xk_print(" vector=");
       put_dec((unsigned long long)r.vector);
       xk_println("");
   }

   xk_print("slices=");
   put_dec(slices);
   xk_print(" reclaimed=");
   put_dec(xproc_reap(&as));
   xk_println("");

   if (owner_retire(owner) != 0) {
       xk_println("[RUN] instance owner still holds tokens: not retired");
   }
   report_shell_budget();

   return st == XPROC_EXIT ? 0 : -1;
}
