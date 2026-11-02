/******************************************************************************
 * XASMOS - EXFS persistence: format, mount, sync
 * Path: source/arch/amd64/drivers/storage/exfs/store.c
 ******************************************************************************/
#include "exfs_internal.h"
#include "store.h"

#define BLOCK_BYTES   4096u
#define NODE_REC      272u
#define TABLE_BLOCKS  9u                       /* 9 * 4096 >= 128 * 272 */
#define TABLE_BYTES   (TABLE_BLOCKS * BLOCK_BYTES)
#define SB_BLOCK      0u
#define TABLE_A_BLOCK 1u
#define TABLE_B_BLOCK (1u + TABLE_BLOCKS)
#define DATA_START    (1u + 2u * TABLE_BLOCKS)
#define VERSION       1u

static const uint8_t MAGIC[8] = { 'X', 'F', 'S', 'D', 'I', 'S', 'K', '1' };

static BlkDev   *g_dev;
static int       g_mounted;
static uint32_t  g_gen;
static uint32_t  g_active;                    /* 0 or 1: the committed table area */
static uint32_t  g_total_blocks;
static uint32_t  g_spb;                       /* sectors per block */
static uint32_t  g_committed[MAX_NODES];      /* blocks the committed state uses */
static uint8_t   g_tbuf[TABLE_BYTES];
static uint8_t   g_blk[BLOCK_BYTES];

/* ---- encoding ----------------------------------------------------------- */

static uint32_t fnv1a(const uint8_t *p, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void sb_encode(uint32_t gen, uint32_t active, uint32_t table_sum) {
    for (uint32_t i = 0; i < BLOCK_BYTES; i++) g_blk[i] = 0;
    for (uint32_t i = 0; i < 8; i++) g_blk[i] = MAGIC[i];
    put32(g_blk + 8, VERSION);
    put32(g_blk + 12, gen);
    put32(g_blk + 16, active);
    put32(g_blk + 20, TABLE_BLOCKS);
    put32(g_blk + 24, DATA_START);
    put32(g_blk + 28, g_total_blocks);
    put32(g_blk + 32, table_sum);
    put32(g_blk + 36, fnv1a(g_blk, 36));
}

/* 0 if the superblock is a valid volume for this device. */
static int sb_decode(uint32_t *gen, uint32_t *active, uint32_t *table_sum) {
    for (uint32_t i = 0; i < 8; i++) {
        if (g_blk[i] != MAGIC[i]) return -1;
    }
    if (get32(g_blk + 8) != VERSION) return -1;
    if (get32(g_blk + 36) != fnv1a(g_blk, 36)) return -1;
    if (get32(g_blk + 20) != TABLE_BLOCKS) return -1;
    if (get32(g_blk + 24) != DATA_START) return -1;
    if (get32(g_blk + 28) != g_total_blocks) return -1;
    *gen = get32(g_blk + 12);
    *active = get32(g_blk + 16);
    *table_sum = get32(g_blk + 32);
    return (*active <= 1u) ? 0 : -1;
}

static void encode_table(const uint32_t *block_of) {
    for (uint32_t i = 0; i < TABLE_BYTES; i++) g_tbuf[i] = 0;
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        uint8_t *rec = g_tbuf + i * NODE_REC;
        const EXFSNode *n = &exfs_table[i];
        if (!n->active) continue;                 /* free slot: all zero */
        for (uint32_t k = 0; k < MAX_PATH_LEN; k++) rec[k] = (uint8_t)n->path[k];
        rec[256] = (uint8_t)n->type;
        rec[257] = 1;
        put32(rec + 260, block_of[i]);
        put32(rec + 264, (uint32_t)n->data_len);
    }
}

/* ---- block I/O ---------------------------------------------------------- */

static int geometry(BlkDev *d) {
    uint32_t ss = blk_sector_size(d);
    if (ss == 0 || BLOCK_BYTES % ss != 0) return -1;
    g_spb = BLOCK_BYTES / ss;
    g_total_blocks = (uint32_t)((blk_sector_count(d) * ss) / BLOCK_BYTES);
    return (g_total_blocks > DATA_START) ? 0 : -1;
}

static int block_read(uint32_t block, uint8_t *buf) {
    return blk_read(g_dev, (uint64_t)block * g_spb, g_spb, buf) == BLK_OK ? 0 : EXFS_ERR_IO;
}

static int block_write(uint32_t block, const uint8_t *buf) {
    return blk_write(g_dev, (uint64_t)block * g_spb, g_spb, buf) == BLK_OK ? 0 : EXFS_ERR_IO;
}

static int table_write(uint32_t base) {
    for (uint32_t b = 0; b < TABLE_BLOCKS; b++) {
        if (block_write(base + b, g_tbuf + b * BLOCK_BYTES) != 0) return EXFS_ERR_IO;
    }
    return 0;
}

static int table_read(uint32_t base) {
    for (uint32_t b = 0; b < TABLE_BLOCKS; b++) {
        if (block_read(base + b, g_tbuf + b * BLOCK_BYTES) != 0) return EXFS_ERR_IO;
    }
    return 0;
}

/* ---- allocation --------------------------------------------------------- */

/* A block is taken if the committed state uses it, or if this sync already
 * gave it to another file. */
static int block_taken(uint32_t b, const uint32_t *planned) {
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (g_committed[i] == b || planned[i] == b) return 1;
    }
    return 0;
}

static uint32_t find_free_block(const uint32_t *planned) {
    for (uint32_t b = DATA_START; b < g_total_blocks; b++) {
        if (!block_taken(b, planned)) return b;
    }
    return 0;
}

static int frame_to_block(const EXFSNode *n, uint32_t b) {
    UINT64 phys = 0;
    if (xtok_ram_info(n->data_tok, &phys, 0) != 0) return EXFS_ERR_IO;
    const uint8_t *page = (const uint8_t *)(UINTN)phys;
    for (uint32_t i = 0; i < BLOCK_BYTES; i++) g_blk[i] = page[i];
    return block_write(b, g_blk);
}

/* ---- public API --------------------------------------------------------- */

int exfs_mounted(void) {
    return g_mounted;
}

int exfs_sync(void) {
    if (!g_mounted) return EXFS_ERR_NOFS;

    int any = exfs_table_dirty;
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        if (exfs_table[i].active && exfs_table[i].data_dirty) any = 1;
    }
    if (!any) return 0;

    /* What the table will say after this commit. Unchanged files keep their
     * block; a slot with no content has none. */
    uint32_t planned[MAX_NODES];
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        planned[i] = (exfs_table[i].active && exfs_table[i].data_len > 0) ? g_committed[i] : 0;
    }

    /* 1. content first: every changed file gets a free block. */
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        EXFSNode *n = &exfs_table[i];
        if (!n->active || n->type != TYPE_FILE || !n->data_dirty) continue;
        if (n->data_len == 0) { planned[i] = 0; continue; }

        uint32_t b = find_free_block(planned);
        if (b == 0) return EXFS_ERR_NOMEM;
        if (frame_to_block(n, b) != 0) return EXFS_ERR_IO;
        planned[i] = b;
    }

    /* 2. the inactive table area. */
    uint32_t target = g_active ^ 1u;
    uint32_t base = (target == 0) ? TABLE_A_BLOCK : TABLE_B_BLOCK;
    encode_table(planned);
    uint32_t table_sum = fnv1a(g_tbuf, TABLE_BYTES);
    if (table_write(base) != 0) return EXFS_ERR_IO;

    /* 3. the superblock: this is the commit point. */
    sb_encode(g_gen + 1u, target, table_sum);
    if (block_write(SB_BLOCK, g_blk) != 0) return EXFS_ERR_IO;

    /* 4. the new state is the committed one. */
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        g_committed[i] = planned[i];
        exfs_table[i].disk_block = planned[i];
        exfs_table[i].data_dirty = 0;
    }
    exfs_table_dirty = 0;
    g_active = target;
    g_gen++;
    return 0;
}

int exfs_mount(BlkDev *d) {
    if (!d) return EXFS_ERR_NOFS;
    g_mounted = 0;
    g_dev = d;
    if (geometry(d) != 0) return EXFS_ERR_NOFS;

    if (block_read(SB_BLOCK, g_blk) != 0) return EXFS_ERR_IO;
    uint32_t gen, active, table_sum;
    if (sb_decode(&gen, &active, &table_sum) != 0) return EXFS_ERR_NOFS;

    uint32_t base = (active == 0) ? TABLE_A_BLOCK : TABLE_B_BLOCK;
    if (table_read(base) != 0) return EXFS_ERR_IO;
    if (fnv1a(g_tbuf, TABLE_BYTES) != table_sum) return EXFS_ERR_NOFS;

    exfs_clear();
    for (uint32_t i = 0; i < MAX_NODES; i++) {
        const uint8_t *rec = g_tbuf + i * NODE_REC;
        g_committed[i] = 0;
        if (rec[257] != 1) continue;

        EXFSNode *n = &exfs_table[i];
        for (uint32_t k = 0; k < MAX_PATH_LEN; k++) n->path[k] = (char)rec[k];
        n->path[MAX_PATH_LEN - 1] = '\0';
        n->type = rec[256];
        n->active = 1;
        n->data_len = (int)get32(rec + 264);
        n->disk_block = get32(rec + 260);
        n->data_dirty = 0;
        g_committed[i] = n->disk_block;

        if (n->disk_block != 0 && n->data_len > 0) {
            n->data_tok = xtok_alloc_ram(XTOK_OWNER_KERNEL, 1, XTOK_RIGHTS_RAM_DEFAULT);
            if (n->data_tok == XTOKEN_INVALID) return EXFS_ERR_NOMEM;
            if (block_read(n->disk_block, g_blk) != 0) return EXFS_ERR_IO;
            UINT64 phys = 0;
            if (xtok_ram_info(n->data_tok, &phys, 0) != 0) return EXFS_ERR_IO;
            uint8_t *page = (uint8_t *)(UINTN)phys;
            for (uint32_t k = 0; k < BLOCK_BYTES; k++) page[k] = g_blk[k];
        }
    }

    if (!exfs_table[0].active || exfs_table[0].type != TYPE_DIR ||
        !exfs_strequal(exfs_table[0].path, "|")) {
        return EXFS_ERR_NOFS;
    }

    g_active = active;
    g_gen = gen;
    exfs_table_dirty = 0;
    exfs_init_mark();
    g_mounted = 1;
    return 0;
}

int exfs_format(BlkDev *d) {
    if (!d) return EXFS_ERR_NOFS;
    g_mounted = 0;
    g_dev = d;
    if (geometry(d) != 0) return EXFS_ERR_NOFS;

    exfs_reset_system();
    for (uint32_t i = 0; i < MAX_NODES; i++) g_committed[i] = 0;
    g_active = 1;                 /* the first commit writes area 0 */
    g_gen = 0;
    exfs_table_dirty = 1;
    g_mounted = 1;

    int rc = exfs_sync();
    if (rc != 0) {
        g_mounted = 0;
        return EXFS_ERR_IO;
    }
    return 0;
}
