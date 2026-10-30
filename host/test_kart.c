/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kart.c
 * @brief Host checks for kart.c on Kody Circuit. Prints "OK <n>".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "kart.h"
#include "track.h"

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

static const kart_stats_t ST = { .top = 140.0f, .accel = 110.0f, .turn = 2.2f };
static const float DT = 1.0f / 50.0f;

/* A skidpad: a huge ring of asphalt, to drift without meeting a wall. */
static const tk_pt_t PAD_PTS[] = { {512, 112}, {912, 512}, {512, 912}, {112, 512} };
static const tk_def_t SKIDPAD = { .name = "skidpad", .pts = PAD_PTS, .n_pts = 4, .half_w = 100, .verge = 10, .laps = 1 };

static uint32_t run(kart_t *k, const track_t *t, kart_input_t in, float secs)
{
    uint32_t ev = 0;
    for ( float s = 0; s < secs; s += DT ) ev |= kart_step(k, &ST, &in, t, DT);
    return ev;
}

int main(void)
{
    track_t t = { 0 };
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    CHECK(track_build(&t, &TK_KODY_CIRCUIT));
    const kart_input_t none = { .gas = true };
    kart_t k;

    /* The throttle is always down: on the straight it reaches the top in a few seconds. */
    const tk_line_t *g = track_at(&t, -40);
    kart_place(&k, g->x, g->y, atan2f(g->dy, g->dx));
    run(&k, &t, none, 1.0f);
    CHECK(60.0f < k.speed && k.speed < ST.top);
    run(&k, &t, none, 2.5f);
    CHECK(ST.top * 0.97f < k.speed && ST.top + 0.01f >= k.speed);
    CHECK(TK_ROAD == k.surface);

    /* Off the throttle it coasts down, slowly. */
    {
        kart_t c = k;
        const kart_input_t off = { 0 };
        run(&c, &t, off, 1.0f);
        CHECK(k.speed * 0.5f > c.speed && k.speed * 0.25f < c.speed);
    }

    /* Braking stops it, then backs it up slowly. */
    kart_input_t brake = { .gas = true, .brake = true };
    run(&k, &t, brake, 1.0f);
    CHECK(0.0f > k.speed && -ST.top * 0.31f <= k.speed);

    /* Grass holds it near half speed. */
    const tk_line_t *v = track_at(&t, 200);
    const float side = TK_KODY_CIRCUIT.half_w + 7.0f;
    kart_place(&k, v->x - v->dy * side, v->y + v->dx * side, atan2f(v->dy, v->dx));
    run(&k, &t, none, 3.0f);
    CHECK(TK_GRASS == k.surface);
    CHECK(ST.top * 0.6f > k.speed && ST.top * 0.4f < k.speed);

    /* Driving straight at the wall: stopped by it, never through it. */
    kart_place(&k, v->x, v->y, atan2f(v->dx, -v->dy));   /* facing right of travel */
    const uint32_t ev = run(&k, &t, none, 4.0f);
    CHECK(ev & KART_EV_WALL);
    CHECK(track_surface(&t, k.x, k.y) != TK_WALL && track_surface(&t, k.x, k.y) != TK_OUT);
    CHECK(abs(track_offset(&t, k.x, k.y)) < TK_KODY_CIRCUIT.half_w + TK_KODY_CIRCUIT.verge + 1);

    /* Steering turns it, the right way (y-down: right is +angle). */
    kart_place(&k, g->x, g->y, 0.0f);
    k.speed = 100.0f;
    kart_input_t right = { .steer = 1.0f, .gas = true };
    run(&k, &t, right, 0.3f);
    CHECK(0.2f < k.angle);

    /* A drift on the skidpad: hop and steer, hold, it charges blue then orange;
     * let go, a turbo past the top. */
    track_t ring = t;   /* the same buffers: Kody Circuit is rebuilt after */
    CHECK(track_build(&ring, &SKIDPAD));
    const tk_line_t *p0 = track_at(&ring, 0);
    kart_place(&k, p0->x, p0->y, atan2f(p0->dy, p0->dx));
    run(&k, &ring, none, 1.5f);
    kart_input_t drift = { .steer = 1.0f, .gas = true, .hop = true };
    uint32_t de = kart_step(&k, &ST, &drift, &ring, DT);
    CHECK(de & KART_EV_HOP);
    CHECK(1 == k.drift);
    CHECK(de & KART_EV_DRIFT);
    de = run(&k, &ring, drift, 1.0f);
    CHECK(de & KART_EV_BLUE);
    CHECK(kart_charge_level(&k) == 1);
    de = run(&k, &ring, drift, 0.9f);
    CHECK(de & KART_EV_ORANGE);
    CHECK(kart_charge_level(&k) == 2);
    de = kart_step(&k, &ST, &none, &ring, DT);
    CHECK(de & KART_EV_MINITURBO);
    CHECK(0.9f < k.boost);
    run(&k, &ring, none, 0.5f);
    CHECK(k.speed > ST.top);

    /* A drift slides: the velocity lags the nose. */
    kart_place(&k, p0->x, p0->y, atan2f(p0->dy, p0->dx));
    run(&k, &ring, none, 1.5f);
    run(&k, &ring, drift, 0.6f);
    const float nose = k.angle, travel = atan2f(k.vy, k.vx);
    CHECK(nose - travel > 0.05f);

    /* A short drift fires nothing. */
    kart_place(&k, p0->x, p0->y, atan2f(p0->dy, p0->dx));
    run(&k, &ring, none, 1.5f);
    run(&k, &ring, drift, 0.3f);
    CHECK(!(kart_step(&k, &ST, &none, &ring, DT) & KART_EV_MINITURBO));
    CHECK(0.0f >= k.boost);

    CHECK(track_build(&t, &TK_KODY_CIRCUIT));

    /* Seeds raise the top speed; a hit spins it and costs two. */
    kart_place(&k, g->x, g->y, atan2f(g->dy, g->dx));
    k.seeds = 10;
    run(&k, &t, none, 4.0f);
    CHECK(ST.top * 1.15f < k.speed);
    kart_hit(&k);
    CHECK(0.0f < k.spin && 8 == k.seeds);
    const float before = k.speed;
    run(&k, &t, none, 0.5f);
    CHECK(before * 0.5f > k.speed);
    kart_hit(&k);          /* already spinning: nothing more */
    CHECK(8 == k.seeds);

    /* A star shrugs a hit off. */
    k.spin = 0.0f;
    k.star = 3.0f;
    kart_hit(&k);
    CHECK(0.0f == k.spin);

    /* A boost pad on the straight fires a turbo. */
    const tk_line_t *pad = track_at(&t, 300);
    kart_place(&k, pad->x + pad->dy * 11.0f, pad->y - pad->dx * 11.0f, atan2f(pad->dy, pad->dx));
    k.speed = 120.0f;
    const uint32_t pe = run(&k, &t, none, 0.6f);
    CHECK(pe & KART_EV_PAD);
    CHECK(0.0f < k.boost || k.speed > ST.top);

    /* Ice: the same turn slides much wider than on asphalt. */
    {
        static const tk_pt_t ICE_PTS[] = { {512, 112}, {912, 512}, {512, 912}, {112, 512} };
        static const tk_feature_t ICE_F[] = { { TK_F_ICE, 0, 3000, -100, 100 } };
        static const tk_def_t RINK = { .name = "rink", .pts = ICE_PTS, .n_pts = 4, .half_w = 100, .verge = 10,
                                       .features = ICE_F, .n_features = 1, .laps = 1 };
        static track_t rink;   /* its own buffers */
        rink.tex = malloc(TK_SIZE * TK_SIZE);
        rink.prog = malloc(TK_SIZE * TK_SIZE * 2);
        rink.off = malloc(TK_SIZE * TK_SIZE);
        rink.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
        CHECK(track_build(&rink, &RINK));
        const tk_line_t *p0 = track_at(&rink, 40);
        CHECK(track_surface(&rink, p0->x, p0->y) == TK_ICE);
        float slip[2];
        for ( int on_ice = 0; 2 > on_ice; on_ice++ )
        {
            const track_t *tt = &ring;
            if ( on_ice )
            {
                tt = &rink;
            }
            if ( !on_ice ) CHECK(track_build(&ring, &SKIDPAD));
            kart_place(&k, p0->x, p0->y, atan2f(p0->dy, p0->dx));
            k.speed = 100.0f;
            k.vx = p0->dx * 100.0f;
            k.vy = p0->dy * 100.0f;
            run(&k, tt, right, 0.4f);
            slip[on_ice] = fabsf(k.angle - atan2f(k.vy, k.vx));
            if ( on_ice ) CHECK(TK_ICE == k.surface);
        }
        CHECK(slip[0] * 2.0f < slip[1]);
    }

    printf("OK %d\n", n_ok);
    return 0;
}
