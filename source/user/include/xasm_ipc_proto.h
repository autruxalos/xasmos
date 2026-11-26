#ifndef XASM_IPC_PROTO_H
#define XASM_IPC_PROTO_H
#include <stdint.h>
#define OP_EXFS_LIST    1u
#define OP_EXFS_MAKE    2u
#define OP_EXFS_REMOVE  3u
#define OP_EXFS_WRITE   4u
#define OP_EXFS_READ    5u
#define OP_EXFS_FORMAT  6u
#define OP_EXFS_MOUNT   7u
#define OP_AUX_SYSINFO  10u
typedef struct {
    uint64_t reply_ep;
    uint32_t op;
    uint32_t flags;
    int32_t  status;
    uint32_t len;
    char     name[48];
    char     data[176];
} XasmIpcMsg;
#endif
