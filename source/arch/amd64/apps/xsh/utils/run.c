/******************************************************************************
 * XASMOS - Coreutil: run. Loads an embedded .exb image and runs it.
 * Path: source/arch/amd64/apps/xsh/utils/run.c
 *
 * The image runs as owner 1 in its own address space. Preemption is enabled
 * with a 10 ms slice. A program that exceeds RUN_MAX_SLICES is stopped and
 * reclaimed, so a loop cannot freeze the shell.
 ******************************************************************************/
#include "common.h"
#include "../images/images.h"
#include <xload.h>
#include <xproc.h>

#define RUN_OWNER      1u
#define RUN_SLICE_MS   10u
#define RUN_MAX_SLICES 500u     /* 5 seconds of CPU at most */

static void list_images(void) {
    int n = xsh_image_count();
    xk_println("embedded images:");
    for (int i = 0; i < n; i++) {
        xk_print("  ");
        xk_println(xsh_image_at(i)->name);
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

    XAspace as;
    if (xas_create(&as, RUN_OWNER) != XAS_OK) {
        xk_println("EXNOMEM: no address space for the program");
        return -1;
    }

    XLoadResult res;
    int rc = xload_exb(&as, RUN_OWNER, img->data, img->len, &res);
    if (rc != XLOAD_OK) {
        xk_print("EXBADIMAGE: load failed, code ");
        put_dec((unsigned long long)(-rc));
        xk_println("");
        xas_destroy(&as);
        return -1;
    }

    XContext ctx;
    XProcResult r;
    xctx_init(&ctx, res.entry, res.stack_top);

    /* If there is no timer, run without preemption: the slice limit below
     * then counts only the first slice, and the program runs to its end. */
    if (xproc_set_quantum_ms(RUN_SLICE_MS) != 0) {
        xproc_set_quantum_ms(0);
    }

    int st = XPROC_PREEMPT;
    unsigned slices = 0;
    while (st == XPROC_PREEMPT && slices < RUN_MAX_SLICES) {
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
        xk_println("EXSTATE: unexpected run state");
    }

    xk_print("slices=");
    put_dec(slices);
    xk_print(" reclaimed=");
    put_dec(xproc_reap(&as));
    xk_println("");

    return st == XPROC_EXIT ? 0 : -1;
}
