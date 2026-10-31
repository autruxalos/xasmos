/******************************************************************************
 * XASMOS - embedded program images for xsh
 * Path: source/arch/amd64/apps/xsh/images/images.c
 *
 * Each image is a validated .exb file compiled into the kernel. The loader
 * validates it again when it runs, so this table is trusted only for its
 * addresses, never for its contents.
 ******************************************************************************/
#include "images.h"
#include "prog_exb.h"

static const XshImage g_images[] = {
    { "prog", prog_exb, prog_exb_len },
};

static int same(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

const XshImage *xsh_image_find(const char *name) {
    for (unsigned i = 0; i < sizeof g_images / sizeof g_images[0]; i++) {
        if (same(g_images[i].name, name)) return &g_images[i];
    }
    return 0;
}

int xsh_image_count(void) {
    return (int)(sizeof g_images / sizeof g_images[0]);
}

const XshImage *xsh_image_at(int i) {
    return &g_images[i];
}
