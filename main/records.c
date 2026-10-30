/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file records.c
 * @brief records_add(): the rules. records_load/save live in records_save.c (the SDK).
 */
#include "records.h"

#include <string.h>

void records_init(records_t *r)
{
    memset(r, 0, sizeof *r);
    r->version = RECORDS_VERSION;
}

static bool better(uint32_t *best, float seconds)
{
    if ( 0.0f >= seconds ) return false;
    const uint32_t cs = (uint32_t)(seconds * 100.0f + 0.5f);
    if ( *best && (*best <= cs) ) return false;
    *best = cs;
    return true;
}

void records_from_v1(records_t *r, const records_v1_t *v1)
{
    records_init(r);
    r->races = v1->races;
    r->wins = v1->wins;
    r->podiums = v1->podiums;
    for ( int m = 0; REC_MODES > m; m++ )
    {
        for ( int c = 0; REC_CCS > c; c++ )
        {
            r->best_race_cs[0][m][c] = v1->best_race_cs[m][c];
            r->best_lap_cs[0][m][c] = v1->best_lap_cs[m][c];
        }
    }
}

uint32_t records_add(records_t *r, int circuit, int mode, int cc, float race_s, float best_lap_s, int place,
                     int racers)
{
    if ( (0 > circuit) || (REC_CIRCUITS <= circuit) || (0 > mode) || (REC_MODES <= mode) || (0 > cc) ||
         (REC_CCS <= cc) )
    {
        return 0;
    }
    uint32_t ev = 0;
    if ( UINT16_MAX > r->races ) r->races++;
    if ( (1 < racers) && (1 == place) && (UINT16_MAX > r->wins) ) r->wins++;
    if ( (1 < racers) && (3 >= place) && (UINT16_MAX > r->podiums) ) r->podiums++;
    if ( better(&r->best_race_cs[circuit][mode][cc], race_s) ) ev |= REC_NEW_RACE;
    if ( better(&r->best_lap_cs[circuit][mode][cc], best_lap_s) ) ev |= REC_NEW_LAP;
    return ev;
}
