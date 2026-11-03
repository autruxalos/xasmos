/******************************************************************************
 * XASMOS - device registry implementation
 * Path: source/core/dev/dev.c
 ******************************************************************************/
#include "dev.h"

static struct {
    char      name[DEV_NAME_MAX + 1];
    BlkDev   *dev;
} g_devs[DEV_MAX];

static int str_eq(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static int name_ok(const char *name) {
    uint32_t n = 0;
    while (name[n] != '\0') n++;
    return n > 0 && n <= DEV_NAME_MAX;
}

int dev_register(const char *name, BlkDev *d) {
    if (!d || !name || !name_ok(name)) return DEV_EINVAL;
    int free_slot = -1;
    for (int i = 0; i < DEV_MAX; i++) {
        if (g_devs[i].dev == 0) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (str_eq(g_devs[i].name, name)) return DEV_EEXIST;
    }
    if (free_slot < 0) return DEV_EFULL;
    uint32_t k = 0;
    while (name[k] != '\0') { g_devs[free_slot].name[k] = name[k]; k++; }
    g_devs[free_slot].name[k] = '\0';
    g_devs[free_slot].dev = d;
    return DEV_OK;
}

BlkDev *dev_lookup(const char *path) {
    const char *p = path;
    const char *pre = DEV_PREFIX;
    while (*pre != '\0') {
        if (*p != *pre) return 0;
        p++;
        pre++;
    }
    for (int i = 0; i < DEV_MAX; i++) {
        if (g_devs[i].dev != 0 && str_eq(g_devs[i].name, p)) return g_devs[i].dev;
    }
    return 0;
}

int dev_list(void (*cb)(const char *name, BlkDev *d)) {
    int n = 0;
    for (int i = 0; i < DEV_MAX; i++) {
        if (g_devs[i].dev == 0) continue;
        cb(g_devs[i].name, g_devs[i].dev);
        n++;
    }
    return n;
}
