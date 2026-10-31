/* XASMOS - Coreutil: tokens. Live hardware tokens in the notary ledger. */
#include "common.h"

extern unsigned int xtok_live_count(void);

int cmd_tokens_main(void) {
    xk_print("tokens live=");
    put_dec(xtok_live_count());
    xk_println("");
    return 0;
}
