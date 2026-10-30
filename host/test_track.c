/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_track.c
 * @brief Host checks for track.c: an oval built, then read back. Prints "OK <n>".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

static const tk_pt_t OVAL[] = { {512, 212}, {812, 512}, {512, 812}, {212, 512} };
static const tk_feature_t FEAT[] = {
    { TK_F_BOOST, 100, 130, -6, 6 },
    { TK_F_SAND, 300, 500, 20, 40 },
};
static const tk_def_t DEF = {
    .name = "oval", .pts = OVAL, .n_pts = 4, .half_w = 20, .verge = 14, .features = FEAT, .n_features = 2, .laps = 3,
};

int main(void)
{
    track_t t = { 0 };
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    CHECK(track_build(&t, &DEF));

    /* A Catmull-Rom through four points of a circle of radius 300 is a little inside it. */
    const float circ = 2.0f * 3.14159f * 300.0f;
    CHECK(fabsf((float)t.length - circ) < circ * 0.10f);

    /* Points are one texel apart and the direction is a unit vector. */
    for ( int i = 0; i < t.length; i++ )
    {
        const tk_line_t *a = &t.line[i], *b = track_at(&t, i + 1);
        CHECK(fabsf(hypotf(b->x - a->x, b->y - a->y) - 1.0f) < 0.05f);
        CHECK(fabsf(hypotf(a->dx, a->dy) - 1.0f) < 1e-3f);
    }

    /* Start: heading right-down from the top; the centre is the checkered line. */
    CHECK(track_surface(&t, 512, 212) == TK_ROAD);
    CHECK(track_prog(&t, 512, 212) < 3);
    const uint8_t c0 = track_texel(&t, 512, 212);
    CHECK(TK_C_CHECK_B == c0 || TK_C_CHECK_W == c0);

    /* The middle of the oval and the corners of the world are out of bounds. */
    CHECK(track_surface(&t, 512, 512) == TK_OUT);
    CHECK(track_surface(&t, 5, 5) == TK_OUT);
    CHECK(track_surface(&t, -3, 50) == TK_OUT);
    CHECK(track_prog(&t, 512, 512) == TK_NO_PROG);

    /* Across the road at three quarters (no feature there): road, verge, wall, out, in that order. */
    const tk_line_t *q = track_at(&t, t.length * 3 / 4);
    const float rx = -q->dy, ry = q->dx;
    CHECK(track_surface(&t, q->x, q->y) == TK_ROAD);
    CHECK(track_surface(&t, q->x + rx * 10, q->y + ry * 10) == TK_ROAD);
    CHECK(track_surface(&t, q->x + rx * 27, q->y + ry * 27) == TK_GRASS);
    CHECK(track_surface(&t, q->x + rx * 36, q->y + ry * 36) == TK_WALL);
    CHECK(track_surface(&t, q->x + rx * 45, q->y + ry * 45) == TK_OUT);
    CHECK(track_offset(&t, q->x + rx * 10, q->y + ry * 10) == 10);
    CHECK(track_offset(&t, q->x - rx * 10, q->y - ry * 10) == -10);

    /* Progress grows along the line. */
    int last = -1;
    for ( int s = 10; t.length - 10 > s; s += 7 )
    {
        const tk_line_t *p = track_at(&t, s);
        const int pr = track_prog(&t, p->x, p->y);
        CHECK(abs(pr - s) <= 1);
        CHECK(pr > last);
        last = pr;
    }

    /* The features: chevrons on the pad, sand on the right verge only. */
    const tk_line_t *b = track_at(&t, 115);
    CHECK(track_surface(&t, b->x, b->y) == TK_BOOST);
    const tk_line_t *sd = track_at(&t, 400);
    CHECK(track_surface(&t, sd->x - sd->dy * 28, sd->y + sd->dx * 28) == TK_SAND);
    CHECK(track_surface(&t, sd->x + sd->dy * 28, sd->y - sd->dx * 28) == TK_GRASS);

    /* A full circle of a constant bend is curvy everywhere: curbs at the edge. */
    CHECK(track_surface(&t, q->x + rx * 20, q->y + ry * 20) == TK_CURB);

    /* Kody Circuit: it builds, its grid is on asphalt, and its shortcut skips
     * less than the progress tracker's jump limit (race.c) but a good deal. */
    CHECK(track_build(&t, &TK_KODY_CIRCUIT));
    CHECK(3000 < t.length && 3500 > t.length);
    for ( int slot = 0; 6 > slot; slot++ )
    {
        const tk_line_t *g = track_at(&t, -(10 + 16 * slot));
        float side = -11.0f;
        if ( 0 != (slot & 1) )
        {
            side = 11.0f;
        }
        CHECK(track_surface(&t, g->x - g->dy * side, g->y + g->dx * side) == TK_ROAD);
    }
    const int in = track_prog(&t, 556, 398), out = track_prog(&t, 426, 398);
    CHECK(TK_NO_PROG != in && TK_NO_PROG != out);
    CHECK(out - in > 150 && out - in < 300);
    CHECK(track_surface(&t, 490, 412) == TK_SAND);

    /* Decor: plenty, all of it off the road and out of the water. */
    CHECK(60 < t.n_decor && TK_MAX_DECOR >= t.n_decor);
    int kinds[TK_D_KINDS] = { 0 };
    for ( int i = 0; i < t.n_decor; i++ )
    {
        const uint8_t c = track_texel(&t, t.decor[i].x, t.decor[i].y);
        CHECK(TK_C_SHORE == c);
        CHECK(track_surface(&t, t.decor[i].x, t.decor[i].y) == TK_OUT);
        kinds[t.decor[i].kind]++;
    }
    for ( int k = 0; TK_D_KINDS > k; k++ ) CHECK(0 < kinds[k]);

    /* Kody Beach: it builds, a beach, its grid on asphalt, its cut a short jump through sand. */
    CHECK(track_build(&t, &TK_KODY_BEACH));
    CHECK(TK_THEME_BEACH == t.def->theme);
    CHECK(2800 < t.length && 3400 > t.length);
    for ( int slot = 0; 6 > slot; slot++ )
    {
        const tk_line_t *g = track_at(&t, -(10 + 16 * slot));
        float side = -10.0f;
        if ( 0 != (slot & 1) )
        {
            side = 10.0f;
        }
        CHECK(track_surface(&t, g->x - g->dy * side, g->y + g->dx * side) == TK_ROAD);
    }
    const int bin = track_prog(&t, 616, 772), bout = track_prog(&t, 458, 784);
    CHECK(bout - bin > 150 && bout - bin < 300);
    CHECK(track_surface(&t, 540, 790) == TK_SAND);
    CHECK(60 < t.n_decor);
    /* Kody Snow: the grid on asphalt, ice on the sweeper and the descent. */
    CHECK(track_build(&t, &TK_KODY_SNOW));
    CHECK(TK_THEME_SNOW == t.def->theme);
    for ( int slot = 0; 6 > slot; slot++ )
    {
        const tk_line_t *g = track_at(&t, -(10 + 16 * slot));
        float side = -10.0f;
        if ( 0 != (slot & 1) )
        {
            side = 10.0f;
        }
        CHECK(track_surface(&t, g->x - g->dy * side, g->y + g->dx * side) == TK_ROAD);
    }
    CHECK(track_surface(&t, track_at(&t, 900)->x, track_at(&t, 900)->y) == TK_ICE);
    CHECK(track_surface(&t, track_at(&t, 1560)->x, track_at(&t, 1560)->y) == TK_ICE);
    for ( int i = 0; TK_CIRCUITS > i; i++ ) CHECK(track_build(&t, TK_ALL[i]));

    printf("OK %d\n", n_ok);
    return 0;
}
