#ifndef XASMOS_Y_PROTO_H
#define XASMOS_Y_PROTO_H
#include <stdint.h>
#define Y_PROTO_MAGIC      0x59564944u /* "YVID" */
#define Y_MAX_SURFACES     16u
#define OP_Y_HELLO          0x2001u
#define OP_Y_CREATE_SURFACE 0x2002u
#define OP_Y_COMMIT         0x2003u
#define OP_Y_MOVE           0x2004u
#define OP_Y_FOCUS          0x2005u
#define OP_Y_DESTROY        0x2006u
typedef struct __attribute__((packed)) {
  uint32_t magic, width, height, pitch, bpp, max_surfaces;
} YHelloInfo;
typedef struct __attribute__((packed)) {
  uint64_t surf_tok; int32_t x, y; uint32_t w, h, stride, flags;
} YCreateSurfaceReq;
typedef struct __attribute__((packed)) {
  uint32_t surf_id, x, y, w, h;
} YCommitReq;
typedef struct __attribute__((packed)) {
  uint32_t surf_id; int32_t x, y;
} YMoveReq;
#endif
