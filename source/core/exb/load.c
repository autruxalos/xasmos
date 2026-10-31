/******************************************************************************
 * XASMOS - .exb loader (portable logic)
 * Path: source/core/exb/load.c
 *
 * Validates the image (exb_parse), checks the owner's quota, then maps each
 * segment and a stack. Every step goes through xlb_* (xload_backend.h), so
 * this file is the same on every architecture.
 *
 * Failure handling: every resource taken is recorded in a transaction and
 * undone in reverse order on the first failure. Nothing survives a failed load.
 *
 * Trust: the image is untrusted. exb_parse() bounds every offset against the
 * image size before a byte is copied, so the copy stays inside the buffer even
 * if the caller changes its contents concurrently.
 ******************************************************************************/
#include "exb.h"
#include "xload.h"
#include "xload_backend.h"
#include "capability.h"

#define LOAD_MAX_HELD  (EXB_MAX_SEGS + 1)   /* one token per segment, plus the stack */

typedef struct {
    XAspace *as;
    uint64_t tok[LOAD_MAX_HELD];
    int      tok_n;
    uint64_t va[LOAD_MAX_HELD];
    int      va_n;
} Txn;

/* Undo in reverse: unmap first, then release. A token is never released
 * while mapped. */
static void rollback(Txn *t) {
    for (int i = t->va_n - 1; i >= 0; i--) xlb_unmap(t->as, t->va[i]);
    for (int i = t->tok_n - 1; i >= 0; i--) xlb_release(t->tok[i]);
    t->va_n = 0;
    t->tok_n = 0;
}

static uint32_t seg_rights(uint32_t flags) {
    uint32_t r = XTOK_RIGHT_READ | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE;
    if (flags & EXB_SEG_W) r |= XTOK_RIGHT_WRITE;
    if (flags & EXB_SEG_X) r |= XTOK_RIGHT_EXECUTE;
    return r;
}

static uint32_t seg_perms(uint32_t flags) {
    uint32_t p = XLB_PERM_R;
    if (flags & EXB_SEG_W) p |= XLB_PERM_W;
    if (flags & EXB_SEG_X) p |= XLB_PERM_X;
    return p;
}

int xload_exb(XAspace *as, uint32_t owner, const void *image, uint64_t size,
              XLoadResult *out) {
    if (!as || !image || !out || size == 0) return XLOAD_E_ARG;
    if (owner == XTOK_OWNER_KERNEL)         return XLOAD_E_ARG;

    ExbInfo info;
    if (exb_parse(image, size, &info) != EXB_OK) return XLOAD_E_IMAGE;

    uint64_t stack_pages = info.stack_bytes / XLB_PAGE;
    uint64_t frames      = info.image_pages + stack_pages;
    uint32_t tokens      = info.seg_count + 1;
    if (!xlb_quota_ok(owner, frames, tokens)) return XLOAD_E_QUOTA;

    Txn t;
    t.as = as;
    t.tok_n = 0;
    t.va_n = 0;

    const uint8_t *img = (const uint8_t *)image;

    for (uint32_t i = 0; i < info.seg_count; i++) {
        const ExbSeg *s = &info.seg[i];
        uint64_t tok;

        if (xlb_alloc(owner, s->pages, seg_rights(s->flags), &tok) != 0) {
            rollback(&t);
            return XLOAD_E_NOMEM;
        }
        t.tok[t.tok_n++] = tok;

        /* Frames are zeroed by xlb_alloc, so the .bss part is already zero. */
        uint8_t *dst = (uint8_t *)xlb_kernel_ptr(tok);
        for (uint32_t b = 0; b < s->file_size; b++) {
            dst[b] = img[s->file_off + b];
        }

        if (xlb_map(as, s->vaddr, tok, seg_perms(s->flags)) != 0) {
            rollback(&t);
            return XLOAD_E_MAP;
        }
        t.va[t.va_n++] = s->vaddr;
    }

    /* Stack: below the guard page at the top of user space. */
    uint64_t stack_top  = XLB_USER_TOP - XLB_PAGE;
    uint64_t stack_base = stack_top - info.stack_bytes;
    uint64_t tok;

    if (xlb_alloc(owner, stack_pages,
                  XTOK_RIGHT_READ | XTOK_RIGHT_WRITE | XTOK_RIGHT_MAP | XTOK_RIGHT_RELEASE,
                  &tok) != 0) {
        rollback(&t);
        return XLOAD_E_NOMEM;
    }
    t.tok[t.tok_n++] = tok;

    if (xlb_map(as, stack_base, tok, XLB_PERM_R | XLB_PERM_W) != 0) {
        rollback(&t);
        return XLOAD_E_MAP;
    }
    t.va[t.va_n++] = stack_base;

    out->entry          = info.entry;
    out->stack_top      = stack_top;
    out->stack_bytes    = info.stack_bytes;
    out->tokens_granted = info.extra_tokens;
    return XLOAD_OK;
}
