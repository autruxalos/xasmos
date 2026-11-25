#include "prog_abi.h"

#define SYS_EXIT          1
#define SYS_FREE          7
#define SYS_DERIVE        13
#define SYS_BUDGET_DERIVE 15
#define SYS_OFFER         16
#define RIGHTS_RRG        (1ull | 2ull | 32ull | 64ull)
#define SHELL_FRAMES      8192ull

PROG_ENTRY(exit_main);

void exit_main(unsigned long long rd0, unsigned long long nd0,
               unsigned long long bud, unsigned long long send_bud,
               unsigned long long send_rdo, unsigned long long send_ndo)
{
    long long c_rd0 = 0, c_nd0 = 0, share = 0;

    if (rd0 && send_rdo &&
        sc4(SYS_DERIVE, (long long)rd0, 0, 128, RIGHTS_RRG, &c_rd0) == 0 && c_rd0) {
        if (sc4(SYS_OFFER, c_rd0, (long long)send_rdo, 0, 0, 0) != 0)
            sc4(SYS_FREE, c_rd0, 0, 0, 0, 0);
    }

    if (nd0 && send_ndo &&
        sc4(SYS_DERIVE, (long long)nd0, 0, 8192, RIGHTS_RRG, &c_nd0) == 0 && c_nd0) {
        if (sc4(SYS_OFFER, c_nd0, (long long)send_ndo, 0, 0, 0) != 0)
            sc4(SYS_FREE, c_nd0, 0, 0, 0, 0);
    }

    if (!bud || !send_bud)
        sc4(SYS_EXIT, 6, 0, 0, 0, 0);

    long long st = sc4(SYS_BUDGET_DERIVE, (long long)bud, SHELL_FRAMES, 0, 0, &share);
    if (st != 0) sc4(SYS_EXIT, 70 + (-st), 0, 0, 0, 0);
    if (share == 0) sc4(SYS_EXIT, 70, 0, 0, 0, 0);

    if (sc4(SYS_OFFER, share, (long long)send_bud, 0, 0, 0) != 0)
        sc4(SYS_EXIT, 8, 0, 0, 0, 0);

    sc4(SYS_EXIT, 0, 0, 0, 0, 0);
}
