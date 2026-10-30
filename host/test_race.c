/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_race.c
 * @brief Host checks for race.c + ai.c: a whole race on Kody Circuit. Prints "OK <n>".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ai.h"
#include "race.h"

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
    track_t t = { 0 };
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    CHECK(track_build(&t, &TK_KODY_CIRCUIT));

    for ( int cc = RACE_50CC; RACE_150CC + 2 >= cc; cc++ )
    {
        /* The last rounds: Kody Beach, then Kody Snow (ice), at 100cc. */
        if ( RACE_150CC + 1 == cc ) CHECK(track_build(&t, &TK_KODY_BEACH));
        if ( RACE_150CC + 2 == cc ) CHECK(track_build(&t, &TK_KODY_SNOW));
        race_cc_t ecc = (race_cc_t)cc;
        if ( RACE_150CC < cc )
        {
            ecc = RACE_100CC;
        }
        race_t r;
        race_init(&r, &t, ecc, 1234u + (uint32_t)cc);
        /* The grid: everyone behind the line on asphalt, the player last. */
        for ( int i = 0; RACE_KARTS > i; i++ )
        {
            CHECK(0 > r.r[i].prog);
            CHECK(track_surface(&t, r.r[i].k.x, r.r[i].k.y) == TK_ROAD);
        }
        CHECK(RACE_KARTS == r.r[0].place);

        const float dt = 1.0f / 60.0f;
        int counts = 0, go = 0, laps = 0, finals = 0, finish = 0;
        kart_input_t in = { 0 };
        uint32_t kev, iev;
        /* While counting, nobody moves. */
        const float x0 = r.r[0].k.x;
        for ( int n = 0; 60 > n; n++ ) race_step(&r, &in, dt, &kev, &iev);
        CHECK(r.r[0].k.x == x0);
        CHECK(race_count(&r) == 2);

        float worst_place_time = 0.0f;
        int drifting = 0;
        for ( int n = 0; 60 * 240 > n && !r.r[0].finished; n++ )
        {
            kart_stats_t st = race_stats(ecc);
            ai_drive(&r, 0, dt, &in, &st);   /* the brain drives the player too */
            const uint32_t ev = race_step(&r, &in, dt, &kev, &iev);
            counts += !!(ev & RACE_EV_COUNT);
            go += !!(ev & RACE_EV_GO);
            laps += !!(ev & RACE_EV_LAP);
            finals += !!(ev & RACE_EV_FINAL_LAP);
            finish += !!(ev & RACE_EV_FINISH);
            /* Places are always a permutation of 1..6. */
            int seen = 0;
            for ( int i = 0; RACE_KARTS > i; i++ ) seen |= 1 << (r.r[i].place - 1);
            CHECK((1 << RACE_KARTS) - 1 == seen);
            for ( int i = 1; RACE_KARTS > i; i++ ) drifting += 0 != r.r[i].k.drift;
            CHECK(!race_wrong_way(&r) || (worst_place_time += dt) < 3.0f);
        }
        printf("  %s %dcc: player finished %d in %.1f s, best lap %.1f s, finishers %d\n", t.def->name, 50 * (ecc + 1),
               r.r[0].place, r.r[0].finish_time, r.r[0].best_lap, r.finishers);
        CHECK(r.r[0].finished);
        if ( RACE_100CC <= ecc ) CHECK(60 < drifting);   /* rivals drift through the long bends */
        CHECK(1 == go);
        CHECK(2 == counts);           /* 2 and 1 after the first second */
        CHECK(2 == laps && 1 == finals && 1 == finish);
        CHECK(10.0f < r.r[0].best_lap && 60.0f > r.r[0].best_lap);
        CHECK(RACE_DONE == r.phase);
        /* The rest finish too, soon after. */
        for ( int n = 0; 60 * 60 > n && RACE_KARTS > r.finishers; n++ ) race_step(&r, &in, dt, &kev, &iev);
        CHECK(RACE_KARTS == r.finishers);
    }
    CHECK(track_build(&t, &TK_KODY_CIRCUIT));

    /* Two karts in the same place are pushed apart; a star spins the other. */
    {
        race_t r;
        race_init(&r, &t, RACE_100CC, 7u);
        kart_input_t in = { 0 };
        uint32_t kev, iev;
        while ( RACE_COUNTING == r.phase ) race_step(&r, &in, 1.0f / 60.0f, &kev, &iev);
        r.r[1].k.x = r.r[0].k.x + 1.0f;
        r.r[1].k.y = r.r[0].k.y;
        r.r[0].k.star = 2.0f;
        const uint32_t ev = race_step(&r, &in, 1.0f / 60.0f, &kev, &iev);
        CHECK(ev & RACE_EV_BUMP);
        CHECK(hypotf(r.r[1].k.x - r.r[0].k.x, r.r[1].k.y - r.r[0].k.y) >= 2.0f * KART_RADIUS - 0.01f);
        CHECK(0.0f < r.r[1].k.spin);
        CHECK(0.0f == r.r[0].k.spin);
    }

    /* The start: gas from just after the 2 is a rocket; from the 3, a flooded engine; none, nothing. */
    {
        const float dt = 1.0f / 60.0f;
        const float when[3] = { 1.2f, 2.8f, -1.0f };   /* seconds before GO the gas goes down; -1 never */
        const uint32_t want[3] = { RACE_EV_ROCKET, RACE_EV_STALL, 0 };
        for ( int c = 0; 3 > c; c++ )
        {
            race_t r;
            race_init(&r, &t, RACE_100CC, 9u);
            uint32_t kev, iev, all = 0;
            while ( RACE_COUNTING == r.phase )
            {
                kart_input_t in = { .gas = 0.0f <= when[c] && -r.clock <= when[c] };
                all |= race_step(&r, &in, dt, &kev, &iev);
            }
            CHECK((all & (RACE_EV_ROCKET | RACE_EV_STALL)) == want[c]);
            if ( 0 == c ) CHECK(0.5f < r.r[0].k.boost);
            if ( 1 == c ) CHECK(0.0f < r.r[0].k.spin);
        }
    }

    printf("OK %d\n", n_ok);
    return 0;
}
