/******************************************************************************
 * XASMOS - kernel harness for the .exb loader
 * Path: tests/arch/amd64/load/load_test_main.c
 *
 * Loads prog.exb (embedded as prog_exb.h), runs it, and checks that every
 * frame and token comes back. Results go to the framebuffer and to COM1.
 * prog.exb exits with code 7.
 ******************************************************************************/
#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "paging.h"
#include "xproc.h"
#include "xload.h"
#include "xload_backend.h"
#include "diag.h"
#include "prog_exb.h"

extern void fb_console_init(FramebufferInfo *fb);
extern void xk_print(const char *s);
extern void xk_println(const char *s);

static int passes, fails;
static unsigned char bad_image[sizeof(prog_exb)];

static void halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

static void check(int ok, const char *name) {
    if (ok) passes++; else fails++;
    serial_puts(ok ? "PASS  " : "FAIL  ");
    serial_puts(name);
    serial_puts("\n");
    xk_print(ok ? "PASS  " : "FAIL  ");
    xk_println(name);
}

static void fb_hex(unsigned long long v) {
    static const char d[] = "0123456789ABCDEF";
    char b[19];
    b[0] = '0';
    b[1] = 'x';
    for (int i = 0; i < 16; i++) b[2 + i] = d[(v >> (60 - 4 * i)) & 0xF];
    b[18] = '\0';
    xk_print(b);
}

void kernel_main(XeretHandoff *hand) {
    fb_console_init(&hand->fb);
    diag_init();
    xk_println("LOAD HARNESS: xload_exb on the real kernel");

    if (phys_init_from_handoff(hand) != 0) { xk_println("FAIL  phys_init"); halt(); }
    xtok_init();
    UINT64 fb_bytes = (UINT64)hand->fb.pixels_per_scanline * 4ULL * hand->fb.height;
    if (xpt_init_identity(hand->phys_end, hand->fb.base, fb_bytes) != 0) {
        xk_println("FAIL  xpt_init_identity");
        halt();
    }
    xproc_init();
    xproc_set_quantum_ms(0);          /* no preemption: the harness runs straight through */

    UINT64 F0 = phys_free_count();    /* baseline, taken after kernel setup */
    XAspace as;
    XLoadResult res;
    XProcResult R;
    XContext ctx;
    UINT64 before;
    int rc;

    /* ---- 1. load prog.exb and run it ---- */
    check(xas_create(&as, 1) == XAS_OK, "owner 1: address space created");
    rc = xload_exb(&as, 1, prog_exb, prog_exb_len, &res);
    check(rc == XLOAD_OK, "owner 1: prog.exb loads");
    check(res.entry == 0x8000000000ULL, "entry is the image entry point");
    check(res.stack_top == XLB_USER_TOP - XLB_PAGE, "stack top sits below the guard page");
    check(res.stack_bytes == 8192, "stack size comes from the manifest");
    check(phys_free_count() < F0, "loading took frames from the allocator");
    check(xtok_live_count() == 2, "two tokens live: the code segment and the stack");
    check(xas_live_mappings() == 2, "two mappings live");

    xctx_init(&ctx, res.entry, res.stack_top);
    int run = xproc_run_ctx(&as, &ctx, &R);
    check(run == XPROC_EXIT, "prog.exb exits normally");
    check(R.exit_code == 7, "prog.exb exit code is 7");

    /* ---- 2. reap: everything the program held must come back ---- */
    check(xproc_reap(&as) == 2, "reap returns both tokens");
    check(phys_free_count() == F0, "every frame is back in the allocator");
    check(xtok_live_count() == 0, "no live tokens");
    check(xas_live_mappings() == 0, "no live mappings");

    /* ---- 3. the same image loads and runs again after a reap ---- */
    check(xas_create(&as, 1) == XAS_OK, "second address space created");
    rc = xload_exb(&as, 1, prog_exb, prog_exb_len, &res);
    check(rc == XLOAD_OK, "second load succeeds: the frames were really reused");
    xctx_init(&ctx, res.entry, res.stack_top);
    run = xproc_run_ctx(&as, &ctx, &R);
    check(run == XPROC_EXIT && R.exit_code == 7, "second run exits with 7");
    xproc_reap(&as);
    check(phys_free_count() == F0, "frames back again after the second reap");

    /* ---- 4. quota: an owner at its limit is refused, and takes nothing ---- */
    xproc_set_quota(2, 1, 256);       /* one frame: the image needs more */
    check(xas_create(&as, 2) == XAS_OK, "owner 2: address space created");
    before = phys_free_count();
    rc = xload_exb(&as, 2, prog_exb, prog_exb_len, &res);
    check(rc == XLOAD_E_QUOTA, "owner 2: quota refuses the image");
    check(phys_free_count() == before, "quota refusal takes no frames");
    check(xtok_live_count() == 0, "quota refusal leaves no token");
    xproc_reap(&as);
    check(phys_free_count() == F0, "frames back after the refused owner is reaped");
    xproc_set_quota(2, 2048, 256);    /* restore the default for owner 2 */

    /* ---- 5. a corrupt image is refused, and leaves nothing behind ---- */
    for (unsigned long i = 0; i < sizeof(prog_exb); i++) bad_image[i] = prog_exb[i];
    bad_image[0] ^= 0xFF;             /* break the magic */
    check(xas_create(&as, 1) == XAS_OK, "owner 1: address space for the corrupt test");
    before = phys_free_count();
    rc = xload_exb(&as, 1, bad_image, prog_exb_len, &res);
    check(rc == XLOAD_E_IMAGE, "corrupt image is refused as XLOAD_E_IMAGE");
    check(phys_free_count() == before, "corrupt image takes no frames");
    check(xtok_live_count() == 0, "corrupt image leaves no token");
    xproc_reap(&as);
    check(phys_free_count() == F0, "frames back after the corrupt test");

    /* ---- summary ---- */
    xk_print("passed="); fb_hex(passes); xk_print("  failed="); fb_hex(fails);
    xk_println("");
    serial_puts(fails ? "RESULT: FAILURES\n" : "RESULT: ALL PASS\n");
    xk_println(fails ? "RESULT: FAILURES" : "RESULT: ALL PASS");
    halt();
}
