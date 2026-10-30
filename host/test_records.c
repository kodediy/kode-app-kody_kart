/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_records.c
 * @brief Host checks for records.c. Prints "OK <n>".
 */
#include <stdio.h>
#include <stdlib.h>

#include "records.h"

static int n_ok;
#define CHECK(c)                                                  \
    do                                                            \
    {                                                             \
        if ( !(c) )                                               \
        {                                                         \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);    \
            exit(1);                                              \
        }                                                         \
        n_ok++;                                                   \
    } while ( 0 )

int main(void)
{
    records_t r;
    records_init(&r);
    CHECK(RECORDS_VERSION == r.version && 0 == r.races);
    /* The first race sets both records. */
    CHECK(records_add(&r, 0, 0, 1, 70.12f, 22.5f, 3, 6) == (REC_NEW_RACE | REC_NEW_LAP));
    CHECK(7012 == r.best_race_cs[0][0][1] && 2250 == r.best_lap_cs[0][0][1]);
    CHECK(1 == r.races && 0 == r.wins && 1 == r.podiums);
    /* Slower: nothing new; faster lap only: the lap. */
    CHECK(records_add(&r, 0, 0, 1, 71.0f, 23.0f, 1, 6) == 0);
    CHECK(1 == r.wins);
    CHECK(records_add(&r, 0, 0, 1, 72.0f, 21.0f, 5, 6) == REC_NEW_LAP);
    /* Another engine or mode keeps its own. */
    CHECK(records_add(&r, 0, 1, 1, 90.0f, 30.0f, 1, 1) == (REC_NEW_RACE | REC_NEW_LAP));
    CHECK(7012 == r.best_race_cs[0][0][1]);
    /* A time trial is no win. */
    CHECK(1 == r.wins && 4 == r.races);
    /* Nonsense is refused. */
    CHECK(records_add(&r, 0, 2, 0, 1.0f, 1.0f, 1, 6) == 0);
    CHECK(records_add(&r, 0, 0, 3, 1.0f, 1.0f, 1, 6) == 0);
    CHECK(records_add(&r, 0, 0, 0, 0.0f, 0.0f, 1, 6) == 0);
    /* Another circuit keeps its own; a bad circuit is refused. */
    CHECK(records_add(&r, 1, 0, 1, 80.0f, 25.0f, 2, 6) == (REC_NEW_RACE | REC_NEW_LAP));
    CHECK(7012 == r.best_race_cs[0][0][1] && 8000 == r.best_race_cs[1][0][1]);
    CHECK(records_add(&r, REC_CIRCUITS, 0, 1, 1.0f, 1.0f, 1, 6) == 0);

    /* A version 1 record carries over as Kody Circuit's. */
    records_v1_t v1 = { .version = 1, .races = 9, .wins = 4, .podiums = 6 };
    v1.best_race_cs[1][2] = 5555;
    v1.best_lap_cs[0][0] = 2222;
    records_from_v1(&r, &v1);
    CHECK(RECORDS_VERSION == r.version && 9 == r.races && 4 == r.wins && 6 == r.podiums);
    CHECK(5555 == r.best_race_cs[0][1][2] && 2222 == r.best_lap_cs[0][0][0] && 0 == r.best_race_cs[1][1][2]);

    printf("OK %d\n", n_ok);
    return 0;
}
