/* XASMOS - .exb v1: Executable XASMOS Binary. Static images only. See the spec
 * in the design notes: 64-byte header, 32-byte segment entries, 16-byte
 * capability entries, CRC-32 over the whole file with bytes 28..31 zeroed. */
#ifndef XASMOS_EXB_H
#define XASMOS_EXB_H
#include <stdint.h>
#include <stddef.h>

#define EXB_VERSION 1
#define EXB_ENDIAN_LITTLE 1
#define EXB_HEADER_SIZE 64
#define EXB_SEG_SIZE 32
#define EXB_CAP_SIZE 16
#define EXB_TARGET_AMD64 1
#define EXB_KIND_USER 0
#define EXB_SEG_R 1u
#define EXB_SEG_W 2u
#define EXB_SEG_X 4u
#define EXB_CAP_STACK 1u
#define EXB_CAP_HEAP_FRAMES 2u
#define EXB_CAP_TOKENS 3u
#define EXB_CAP_OPTIONAL 0x80000000u
#define EXB_PAGE 4096ULL
#define EXB_MAX_FILE (16u * 1024u * 1024u)
#define EXB_MAX_SEGS 8
#define EXB_MAX_CAPS 8
#define EXB_MAX_IMAGE_PAGES 4096u
#define EXB_MAX_IMAGE_BYTES ((uint64_t)EXB_MAX_IMAGE_PAGES * EXB_PAGE)
#define EXB_DEFAULT_STACK_PAGES 4u
#define EXB_MAX_STACK_PAGES 64u
#define EXB_MAX_HEAP_REQUEST (1u << 20)
#define EXB_MAX_TOKEN_REQUEST 4096u
#define EXB_VA_MIN 0x0000008000000000ULL
#define EXB_VA_LIMIT 0x0000700000000000ULL

typedef enum {
    EXB_OK = 0, EXB_E_SIZE = -1, EXB_E_MAGIC = -2, EXB_E_VERSION = -3, EXB_E_HEADER = -4,
    EXB_E_TARGET = -5, EXB_E_CRC = -6, EXB_E_SEGTAB = -7, EXB_E_SEG = -8, EXB_E_WX = -9,
    EXB_E_OVERLAP = -10, EXB_E_ENTRY = -11, EXB_E_CAP = -12, EXB_E_LIMIT = -13, EXB_E_KIND = -14,
    EXB_E_NOMEM = -20, EXB_E_LOAD = -21
} ExbStatus;

typedef struct {
    uint64_t vaddr, mem_size, pages;
    uint32_t file_off, file_size, flags;
} ExbSeg;

typedef struct {
    uint64_t entry;
    uint32_t seg_count;
    ExbSeg seg[EXB_MAX_SEGS];
    uint64_t image_pages;
    uint32_t stack_bytes;
    uint64_t heap_frames;
    uint32_t extra_tokens;
} ExbInfo;

uint32_t exb_crc32_image(const uint8_t *img, size_t size);
int exb_parse(const void *image, uint64_t size, ExbInfo *out);
#endif
