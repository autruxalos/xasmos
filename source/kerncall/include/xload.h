/******************************************************************************
 * XASMOS - .exb loader contract (xeret)
 * Path: source/kerncall/include/xload.h
 *
 * This header is architecture-neutral. It names what the loader does, not how
 * an architecture maps memory.
 *
 * XAspace is opaque here. Each architecture defines it with the SAME tag:
 *
 *     typedef struct XAspace { ...arch fields... } XAspace;
 *
 * An anonymous struct (typedef struct { ... } XAspace;) conflicts with the
 * forward declaration below and does not compile when both headers are
 * included. This is the rule every arch header must follow.
 *
 * Guarantee (all or nothing):
 *   On success, the image is mapped in the address space and the program is
 *   ready to run at result->entry with stack at result->stack_top.
 *   On ANY failure, nothing the loader did remains: no frame is held, no
 *   token is live, no mapping exists. The caller never has to clean up after
 *   a failed load.
 *
 * Trust model:
 *   The image is untrusted input. It is validated by exb_parse() before a
 *   single frame is requested. The owner id comes from the kernel, never from
 *   the image.
 ******************************************************************************/
#ifndef XASMOS_XLOAD_H
#define XASMOS_XLOAD_H

#include <stdint.h>

/* Per-architecture address space. The arch must define it with this tag. */
typedef struct XAspace XAspace;

/* Load status. Negative values are failures. */
#define XLOAD_OK          0
#define XLOAD_E_ARG      (-1)   /* null pointer, zero size, or owner 0          */
#define XLOAD_E_IMAGE    (-2)   /* exb_parse() rejected the image                */
#define XLOAD_E_QUOTA    (-3)   /* the owner's frame or token quota would break  */
#define XLOAD_E_NOMEM    (-4)   /* the allocator or the mapping table is full    */
#define XLOAD_E_MAP      (-5)   /* a segment could not be mapped with its perms  */

typedef struct {
    uint64_t entry;          /* virtual address of the first instruction      */
    uint64_t stack_top;      /* initial stack pointer (16-byte aligned)       */
    uint32_t stack_bytes;    /* stack size granted, from the manifest or default */
    uint32_t tokens_granted; /* extra tokens the manifest asked for, reserved  */
} XLoadResult;

/* Loads a validated .exb image into `as` for owner `owner`.
 * `image` is `size` bytes of untrusted data; the loader copies from it and
 * never keeps a pointer to it. Returns XLOAD_OK or a negative XLOAD_E_*. */
int xload_exb(XAspace *as, uint32_t owner, const void *image, uint64_t size,
              XLoadResult *out);

#endif /* XASMOS_XLOAD_H */
