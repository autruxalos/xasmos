#include "efi_types.h"
#include "kmemory.h"
#include "capability.h"
#include "diag.h"
#include "exfs.h"

extern void fb_console_init(FramebufferInfo *fb);

static int fails;
static char big[5000];
static char out[EXFS_DATA_MAX + 8];

static void check(const char *name, int cond) {
    serial_puts(cond ? "PASS  " : "FAIL  ");
    serial_puts(name);
    serial_puts("\n");
    if (!cond) fails++;
}

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

void kernel_main(XeretHandoff *hand) {
    fb_console_init(&hand->fb);
    diag_init();
    if (phys_init_from_handoff(hand) != 0) { serial_puts("FAIL  phys_init\n"); for(;;) __asm__ volatile("hlt"); }
    xtok_init();

    UINT64 f0 = phys_free_count();
    serial_puts("baseline free frames: "); serial_hex(f0); serial_puts("\n");

    /* --- empty file costs no memory --- */
    check("create file", exfs_create_object("|a", 0) == 0);
    check("create allocates no frame", phys_free_count() == f0);
    check("read of never-written file is empty", exfs_read_data("|a", out, sizeof(out)) == 0 && out[0] == '\0');

    /* --- first write takes exactly one frame --- */
    check("small write ok", exfs_write_data("|a", "hello") == 0);
    check("first write takes exactly 1 frame", phys_free_count() == f0 - 1);
    check("1 live token", xtok_live_count() == 1);
    check("read back 'hello'", exfs_read_data("|a", out, sizeof(out)) == 5 && streq(out, "hello"));

    /* --- overwrite reuses the frame --- */
    check("overwrite ok", exfs_write_data("|a", "second, longer content here") == 0);
    check("overwrite does not take another frame", phys_free_count() == f0 - 1);
    check("still 1 live token", xtok_live_count() == 1);
    check("read back overwrite", streq((exfs_read_data("|a", out, sizeof(out)), out), "second, longer content here"));

    /* --- shorter overwrite must not expose the tail of the old content --- */
    check("shorter overwrite ok", exfs_write_data("|a", "hi") == 0);
    check("shorter overwrite reads exactly 'hi'", exfs_read_data("|a", out, sizeof(out)) == 2 && streq(out, "hi"));

    /* --- capacity: 5000 bytes -> truncated to 4095, still stored --- */
    for (int i = 0; i < 4999; i++) big[i] = 'x';
    big[4999] = '\0';
    check("5000-byte write reports truncation", exfs_write_data("|a", big) == EXFS_ERR_TRUNC);
    check("truncated length is exactly 4095", exfs_read_data("|a", out, sizeof(out)) == 4095 && slen(out) == 4095);
    int all_x = 1; for (int i = 0; i < 4095; i++) if (out[i] != 'x') all_x = 0;
    check("truncated content intact", all_x);
    check("truncation did not take another frame", phys_free_count() == f0 - 1);

    /* --- exact boundary: 4095 bytes fits with no truncation --- */
    big[4095] = '\0';
    check("4095-byte write fits exactly", exfs_write_data("|a", big) == 0);

    /* --- read into a too-small buffer is bounded --- */
    char small[8];
    check("small buffer read is bounded to max-1", exfs_read_data("|a", small, sizeof(small)) == 7 && slen(small) == 7);

    /* --- second file, independent frame --- */
    exfs_create_object("|b", 0);
    check("write file b", exfs_write_data("|b", "bee") == 0);
    check("2 frames in use", phys_free_count() == f0 - 2);
    check("2 live tokens", xtok_live_count() == 2);
    check("a and b are independent", exfs_read_data("|b", out, sizeof(out)) == 3 && streq(out, "bee"));

    /* --- delete returns the frame --- */
    check("delete a", exfs_delete_object("|a") == 0);
    check("delete a returns its frame", phys_free_count() == f0 - 1);
    check("1 live token after delete", xtok_live_count() == 1);
    check("deleted file is gone", exfs_stat("|a") == -1);
    check("delete b", exfs_delete_object("|b") == 0);
    check("all frames back", phys_free_count() == f0);
    check("0 live tokens", xtok_live_count() == 0);

    /* --- deleting a never-written file touches no frames --- */
    exfs_create_object("|empty", 0);
    check("delete never-written file ok", exfs_delete_object("|empty") == 0);
    check("...and frame count unchanged", phys_free_count() == f0);

    /* --- recycled node slot must not leak the previous file's data --- */
    exfs_create_object("|old", 0);
    exfs_write_data("|old", "SECRET-OLD-CONTENT");
    exfs_delete_object("|old");
    exfs_create_object("|new", 0);
    check("recycled slot reads empty, no stale data", exfs_read_data("|new", out, sizeof(out)) == 0 && out[0] == '\0');
    exfs_delete_object("|new");

    /* --- wrong-kind / missing targets --- */
    exfs_create_object("|d", 1);
    check("write to a directory is refused", exfs_write_data("|d", "x") == EXFS_ERR_NOTFILE);
    check("read of a directory is refused", exfs_read_data("|d", out, sizeof(out)) == EXFS_ERR_NOTFILE);
    check("write to a missing path is refused", exfs_write_data("|nope", "x") == EXFS_ERR_NOTFILE);
    exfs_delete_object("|d");

    /* --- memory exhaustion: write must fail cleanly, not corrupt --- */
    exfs_create_object("|full", 0);
    phys_reserve_range(0, hand->phys_end);          /* mark every frame used */
    check("allocator is exhausted", phys_free_count() == 0);
    check("write with no frames returns EXFS_ERR_NOMEM", exfs_write_data("|full", "x") == EXFS_ERR_NOMEM);
    check("failed write left the file empty", exfs_read_data("|full", out, sizeof(out)) == 0 && out[0] == '\0');
    check("failed write leaked no token", xtok_live_count() == 0);

    serial_puts(fails == 0 ? "RESULT: ALL PASS\n" : "RESULT: FAILURES\n");
    serial_hex((unsigned long long)fails); serial_puts(" failing\n");
    for (;;) { __asm__ volatile ("hlt"); }
}
