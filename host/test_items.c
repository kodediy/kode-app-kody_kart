/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_items.c
 * @brief Host checks for items.c on Kody Circuit. Prints "OK <n>".
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

static const float DT = 1.0f / 60.0f;

static void go(race_t *r)
{
    kart_input_t in = { 0 };
    uint32_t a, b;
    while ( RACE_COUNTING == r->phase ) race_step(r, &in, DT, &a, &b);
}

/** @brief Put racer i on the centre line at s, facing along it, at speed. */
static void put(race_t *r, int i, int s, float off, float speed)
{
    const tk_line_t *l = track_at(r->t, s);
    kart_t *k = &r->r[i].k;
    kart_place(k, l->x - l->dy * off, l->y + l->dx * off, atan2f(l->dy, l->dx));
    k->speed = speed;
    k->vx = l->dx * speed;
    k->vy = l->dy * speed;
    r->r[i].prog = s;
}

int main(void)
{
    track_t t = { 0 };
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    CHECK(track_build(&t, &TK_KODY_CIRCUIT));

    /* The places' weights: the leader never gets the golden seed, the last never a peel. */
    int lead[IT_COUNT] = { 0 }, last[IT_COUNT] = { 0 };
    for ( int n = 0; 1000 > n; n++ )
    {
        lead[item_for_place(1, 6, n / 1000.0f)]++;
        last[item_for_place(6, 6, n / 1000.0f)]++;
    }
    CHECK(0 == lead[IT_GOLDEN] && 500 < lead[IT_PEEL]);
    CHECK(0 == last[IT_PEEL] && last[IT_TURBO] + last[IT_GOLDEN] > 600);
    CHECK(0 == lead[IT_NONE] && 0 == last[IT_NONE]);

    race_t r;
    race_init(&r, &t, RACE_100CC, 99u);
    CHECK(9 == r.items.n_boxes);       /* three rows of three */
    CHECK(25 == r.items.n_seeds);      /* five rows of five */
    for ( int b = 0; b < r.items.n_boxes; b++ )
        CHECK(track_surface(&t, r.items.boxes[b].x, r.items.boxes[b].y) == TK_ROAD);
    for ( int s = 0; s < r.items.n_seeds; s++ )
    {
        const tk_surface_t su = track_surface(&t, r.items.seeds[s].x, r.items.seeds[s].y);
        CHECK(TK_ROAD == su || TK_CURB == su);
    }
    go(&r);

    /* A box: the roulette, then an item in hand; the box comes back later. */
    for ( int i = 1; RACE_KARTS > i; i++ )
    {
        put(&r, i, 1000 + 100 * i, 0, 0);   /* out of the way */
    }
    put(&r, 0, 160, -5, 100.0f);
    kart_input_t in = { .gas = true };
    uint32_t kev, iev, all = 0;
    for ( int n = 0; 30 > n; n++ )
    {
        race_step(&r, &in, DT, &kev, &iev);
        all |= iev;
    }
    CHECK(all & ITEM_EV_BOX);
    CHECK(IT_NONE != r.r[0].item && 0.0f < r.r[0].roll);
    all = 0;
    for ( int n = 0; 70 > n; n++ )
    {
        race_step(&r, &in, DT, &kev, &iev);
        all |= iev;
    }
    CHECK(all & ITEM_EV_GOT);
    CHECK(0.0f == r.r[0].roll);
    /* The straight's row is boxes 0..2 at -16, -5, 6: the middle one was taken, not its neighbours. */
    CHECK(0.0f < r.items.boxes[1].t);
    CHECK(0.0f == r.items.boxes[0].t && 0.0f == r.items.boxes[2].t);

    /* A seed row: seeds collected, up to the top. */
    put(&r, 0, 460, 12, 100.0f);
    all = 0;
    for ( int n = 0; 60 > n; n++ )
    {
        race_step(&r, &in, DT, &kev, &iev);
        all |= iev;
    }
    CHECK(all & ITEM_EV_SEED);
    CHECK(3 <= r.r[0].k.seeds);

    /* A peel dropped by racer 1 spins racer 0 driving into it. */
    put(&r, 1, 2000, 0, 0);
    r.r[1].item = IT_PEEL;
    CHECK(race_use_item(&r, 1) == 0);   /* not the player's: no player event */
    CHECK(IT_NONE == r.r[1].item);
    put(&r, 1, 1500, 0, 0);
    put(&r, 0, 1960, 0, 110.0f);
    r.r[0].k.star = 0.0f;
    all = 0;
    for ( int n = 0; 40 > n; n++ )
    {
        race_step(&r, &in, DT, &kev, &iev);
        all |= iev;
    }
    CHECK(all & ITEM_EV_HIT);

    /* Hopping over a peel is safe. */
    put(&r, 1, 2300, 0, 0);
    r.r[1].item = IT_PEEL;
    race_use_item(&r, 1);
    put(&r, 1, 1500, 0, 0);
    put(&r, 0, 2280, 0, 110.0f);
    r.r[0].k.spin = 0.0f;
    all = 0;
    for ( int n = 0; 15 > n; n++ )
    {
        r.r[0].k.hop = 0.2f;
        race_step(&r, &in, DT, &kev, &iev);
        all |= iev;
    }
    CHECK(!(all & ITEM_EV_HIT));

    /* The player's acorn rolls along the track - round the bend - and strikes the kart ahead. */
    for ( int n = 0; ITEM_MAX_SHOTS > n; n++ ) r.items.shots[n].kind = IT_NONE;
    put(&r, 0, 560, 0, 0);
    put(&r, 2, 760, 0, 0);    /* past the sweeper's start */
    r.r[0].item = IT_ACORN;
    r.r[0].k.spin = 0.0f;
    r.r[2].k.spin = 0.0f;
    kart_input_t fire = { .gas = true, .use = true };
    race_step(&r, &fire, DT, &kev, &iev);
    CHECK(iev & ITEM_EV_USE);
    all = 0;
    for ( int n = 0; 90 > n; n++ )
    {
        put(&r, 2, 760, 0, 0);     /* the target waits */
        r.r[0].k.speed = 0;
        race_step(&r, &in, DT, &kev, &iev);
        all |= iev;
    }
    CHECK(all & ITEM_EV_STRUCK);
    int flying = 0;
    for ( int n = 0; ITEM_MAX_SHOTS > n; n++ ) flying += IT_ACORN == r.items.shots[n].kind;
    CHECK(0 == flying);   /* spent on the hit */

    /* A golden seed: invincible for a while, a peel does nothing. */
    r.r[0].item = IT_GOLDEN;
    race_use_item(&r, 0);
    CHECK(5.0f < r.r[0].k.star && 0.0f < r.r[0].k.boost);
    r.r[0].k.spin = 0.0f;
    kart_hit(&r.r[0].k);
    CHECK(0.0f == r.r[0].k.spin);

    /* Rivals use their items: a turbo on the start straight goes off within a second. */
    {
        race_t q;
        race_init(&q, &t, RACE_100CC, 3u);
        go(&q);
        put(&q, 1, 200, 0, 100.0f);
        q.r[1].item = IT_TURBO;
        kart_input_t idle = { 0 };
        for ( int n = 0; 60 > n && IT_NONE != q.r[1].item; n++ ) race_step(&q, &idle, DT, &kev, &iev);
        CHECK(IT_NONE == q.r[1].item);
        CHECK(0.0f < q.r[1].k.boost);
        /* An acorn held with nobody ahead is kept a while, then thrown anyway. */
        put(&q, 1, 2000, 0, 0);
        for ( int j = 2; RACE_KARTS > j; j++ ) put(&q, j, 400 + 20 * j, 0, 0);
        put(&q, 0, 500, 0, 0);
        q.r[1].item = IT_ACORN;
        q.r[1].item_t = 0.0f;
        for ( int n = 0; 60 > n; n++ ) race_step(&q, &idle, DT, &kev, &iev);
        CHECK(IT_ACORN == q.r[1].item);
        int thrown_at = 0;
        for ( int n = 60; 60 * 10 > n && !thrown_at; n++ )
        {
            race_step(&q, &idle, DT, &kev, &iev);
            if ( IT_NONE == q.r[1].item ) thrown_at = n;
        }
        CHECK(60 * 7.5f < thrown_at && 60 * 9 > thrown_at);
    }

    /* A time trial: alone, no boxes or seeds, three turbos used one by one. */
    {
        race_t q;
        race_init(&q, &t, RACE_150CC, 11u);
        race_time_trial(&q);
        CHECK(1 == q.n && 0 == q.items.n_boxes && 0 == q.items.n_seeds);
        CHECK(IT_TURBO == q.r[0].item && 3 == q.r[0].stock);
        go(&q);
        kart_input_t fire = { .gas = true, .use = true }, gas = { .gas = true };
        for ( int n = 0; 3 > n; n++ )
        {
            race_step(&q, &fire, DT, &kev, &iev);
            CHECK(iev & ITEM_EV_USE);
            for ( int m = 0; 30 > m; m++ ) race_step(&q, &gas, DT, &kev, &iev);
        }
        CHECK(IT_NONE == q.r[0].item);
        race_step(&q, &fire, DT, &kev, &iev);
        CHECK(!(iev & ITEM_EV_USE));
        /* It still runs to the finish, first of one. */
        for ( int n = 0; 60 * 120 > n && !q.r[0].finished; n++ )
        {
            kart_stats_t st = race_stats(RACE_150CC);
            kart_input_t in2;
            ai_drive(&q, 0, DT, &in2, &st);
            race_step(&q, &in2, DT, &kev, &iev);
        }
        CHECK(q.r[0].finished && 1 == q.r[0].place);
    }

    printf("OK %d\n", n_ok);
    return 0;
}
