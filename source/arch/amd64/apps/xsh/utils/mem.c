/* XASMOS - Coreutil: mem. Physical frame usage as the allocator sees it. */
#include "common.h"

extern unsigned long long phys_free_count(void);
extern unsigned long long phys_total_count(void);

int cmd_mem_main(void) {
    unsigned long long total = phys_total_count();
    unsigned long long free  = phys_free_count();
    unsigned long long used  = total - free;

    xk_print("frames total=");  put_dec(total);
    xk_print("  used=");        put_dec(used);
    xk_print("  free=");        put_dec(free);
    xk_print("  free-MiB=");    put_dec(free * 4 / 1024);
    xk_println("");
    return 0;
}
