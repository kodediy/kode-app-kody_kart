/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file track.c
 * @brief track_build(): a closed spline into a texture and a progress map.
 *
 * Three passes. The spline is sampled densely and resampled one texel of arc
 * apart (the centre line, line[]). Then a band is stamped across every
 * half-texel of it: a texel keeps the centre-line point it is nearest to
 * (prog) and how far off it lies (off). Then every stamped texel is coloured
 * from (prog, off) and the circuit's features. A sand shortcut is stamped last,
 * through whatever was there.
 */
#include "track.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WALL_W      4      /* tyre wall thickness, texels */
#define SHORE_W     18     /* the strip behind the wall the decor stands on */
#define SUBSTEPS    96     /* dense samples per spline segment */
#define CURVY       0.010f /* turn rate (rad/texel) above which the edge is a curb */
#define CURV_SPAN   12     /* curvature is measured over +-this many texels */

static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void catmull(const tk_pt_t *p0, const tk_pt_t *p1, const tk_pt_t *p2, const tk_pt_t *p3, float t,
                    float *x, float *y)
{
    const float t2 = t * t, t3 = t2 * t;
    *x = 0.5f * ((2 * p1->x) + (-p0->x + p2->x) * t + (2 * p0->x - 5 * p1->x + 4 * p2->x - p3->x) * t2 +
                 (-p0->x + 3 * p1->x - 3 * p2->x + p3->x) * t3);
    *y = 0.5f * ((2 * p1->y) + (-p0->y + p2->y) * t + (2 * p0->y - 5 * p1->y + 4 * p2->y - p3->y) * t2 +
                 (-p0->y + 3 * p1->y - 3 * p2->y + p3->y) * t3);
}

/** @brief Walk the spline densely: its arc length, and with @p out a point every @p step of it. */
static float walk(const tk_def_t *d, float step, tk_line_t *out, int *count)
{
    const int n = d->n_pts;
    float px = d->pts[0].x, py = d->pts[0].y, arc = 0.0f, next = step;
    int k = 0;
    if ( out )
    {
        out[0].x = px;
        out[0].y = py;
        k = 1;
    }
    for ( int i = 0; i < n; i++ )
    {
        const tk_pt_t *p0 = &d->pts[(i + n - 1) % n], *p1 = &d->pts[i], *p2 = &d->pts[(i + 1) % n],
                      *p3 = &d->pts[(i + 2) % n];
        for ( int j = 1; SUBSTEPS >= j; j++ )
        {
            float x, y;
            catmull(p0, p1, p2, p3, (float)j / SUBSTEPS, &x, &y);
            const float seg = hypotf(x - px, y - py);
            /* Emit a point every `step` of arc along this little chord. */
            while ( (NULL != out) && (arc + seg >= next) && (k < *count) )
            {
                const float f = (next - arc) / seg;
                out[k].x = px + (x - px) * f;
                out[k].y = py + (y - py) * f;
                k++;
                next += step;
            }
            arc += seg;
            px = x;
            py = y;
        }
    }
    if ( out )
    {
        *count = k;
    }
    return arc;
}

/** @brief The centre line: N points, N the lap in whole texels, evenly apart, from pts[0] round. */
static bool sample_line(track_t *t)
{
    const float lap = walk(t->def, 1.0f, NULL, NULL);
    const int n = (int)lrintf(lap);
    if ( (16 > n) || (TK_MAX_LINE < n) )
    {
        return false;
    }
    int k = n;
    walk(t->def, lap / (float)n, t->line, &k);
    t->length = k;
    for ( int i = 0; i < k; i++ )
    {
        const tk_line_t *a = &t->line[(i + k - 1) % k], *b = &t->line[(i + 1) % k];
        float dx = b->x - a->x, dy = b->y - a->y;
        const float l = hypotf(dx, dy);
        /* Two points on top of each other: any unit direction will do. */
        if ( 0 < l )
        {
            t->line[i].dx = dx / l;
            t->line[i].dy = dy / l;
        }
        else
        {
            t->line[i].dx = 1.0f;
            t->line[i].dy = 0.0f;
        }
    }
    for ( int i = 0; i < k; i++ )
    {
        const tk_line_t *a = &t->line[(i + k - CURV_SPAN) % k], *b = &t->line[(i + CURV_SPAN) % k];
        float da = atan2f(b->dy, b->dx) - atan2f(a->dy, a->dx);
        while ( (float)M_PI < da )
        {
            da -= 2.0f * (float)M_PI;
        }
        while ( -(float)M_PI > da )
        {
            da += 2.0f * (float)M_PI;
        }
        t->line[i].curv = da / (2.0f * CURV_SPAN);
    }
    return true;
}

/**
 * @brief Every half-texel of the line stamps its band; the nearest point wins.
 *
 * Nearest by true distance from the texel's centre, kept x4 in tex[] (which
 * is scratch until the colouring pass): the lateral offset alone ties along
 * the line and would hand a texel to whichever point came first. Texel x
 * covers [x, x + 1): its centre is x + 0.5, as track_prog() reads it.
 */
static void stamp(track_t *t)
{
    const tk_def_t *d = t->def;
    const int band = d->half_w + d->verge + WALL_W + SHORE_W;
    for ( int i = 0; i < t->length; i++ )
    {
        const tk_line_t *a = &t->line[i], *b = &t->line[(i + 1) % t->length];
        for ( int h = 0; 2 > h; h++ )
        {
            const float f = 0.5f * (float)h;
            const float px = a->x + (b->x - a->x) * f, py = a->y + (b->y - a->y) * f;
            /* Right of the direction of travel, in a y-down world. */
            const float rx = -(a->dy + (b->dy - a->dy) * f), ry = a->dx + (b->dx - a->dx) * f;
            for ( int o2 = -2 * band; 2 * band >= o2; o2++ )
            {
                const float o = 0.5f * (float)o2;
                const int x = (int)floorf(px + rx * o), y = (int)floorf(py + ry * o);
                if ( (TK_SIZE <= (unsigned)x) || (TK_SIZE <= (unsigned)y) )
                {
                    continue;
                }
                const int idx = y * TK_SIZE + x;
                const float ex = (float)x + 0.5f - px, ey = (float)y + 0.5f - py;
                const int d4 = (int)(4.0f * sqrtf(ex * ex + ey * ey));
                if ( (255 > d4) && (t->tex[idx] > d4) )
                {
                    t->tex[idx] = (uint8_t)d4;
                    int oi = (int)lrintf(ex * rx + ey * ry);
                    /* off[] is an int8_t: clamp to its range. */
                    if ( 127 < oi )
                    {
                        oi = 127;
                    }
                    else if ( -127 > oi )
                    {
                        oi = -127;
                    }
                    t->prog[idx] = (uint16_t)i;
                    t->off[idx] = (int8_t)oi;
                }
            }
        }
    }
}

static bool in_span(const tk_feature_t *f, int s, int o)
{
    if ( (f->o0 > o) || (f->o1 < o) )
    {
        return false;
    }
    if ( f->s0 <= f->s1 )
    {
        return (f->s0 <= s) && (f->s1 > s);
    }
    return (f->s0 <= s) || (f->s1 > s);   /* wraps over the start line */
}

/** @brief One of two colours: @p rare when @p h's low bits under @p mask are all zero. */
static uint8_t speckle(uint32_t h, uint32_t mask, uint8_t rare, uint8_t usual)
{
    if ( 0 == (h & mask) )
    {
        return rare;
    }
    return usual;
}

/** @brief One of two colours, by whether @p bit is set. */
static uint8_t pick(int bit, uint8_t set, uint8_t clear)
{
    if ( bit )
    {
        return set;
    }
    return clear;
}

static uint8_t colour(const track_t *t, int x, int y, int s, int o)
{
    const tk_def_t *d = t->def;
    const int a = abs(o), hw = d->half_w;
    const bool curvy = CURVY < fabsf(t->line[s].curv);
    const uint32_t h = hash2(x, y);

    /* A bend's curb sticks out two texels past the road. */
    int road = hw;
    if ( curvy )
    {
        road = hw + 2;
    }
    if ( road >= a )
    {
        if ( hw - 2 < a )
        {
            if ( curvy )
            {
                return pick((s / 6) & 1, TK_C_CURB_R, TK_C_CURB_W);
            }
            return pick(hw - 1 < a, TK_C_LINE, TK_C_ASPHALT);
        }
        /* The checkered line, three texels a square, six texels deep. */
        if ( 6 > s )
        {
            return pick(((s / 3) + ((o + 128) / 3)) & 1, TK_C_CHECK_B, TK_C_CHECK_W);
        }
        for ( int i = 0; i < d->n_features; i++ )
        {
            const tk_feature_t *f = &d->features[i];
            if ( (TK_F_ICE == f->kind) && in_span(f, s, o) )
            {
                return speckle(h, 7, TK_C_ICE2, TK_C_ICE);
            }
            if ( (TK_F_BOOST != f->kind) || (!in_span(f, s, o)) )
            {
                continue;
            }
            const int mid = (f->o0 + f->o1) / 2;
            /* Chevrons pointing the way: a V of stripes. */
            return pick(4 > (((s - f->s0) + abs(o - mid)) & 7), TK_C_BOOST, TK_C_BOOST2);
        }
        /* The grid: a white bracket per starting slot behind the line. */
        const int behind = t->length - s;
        if ( (10 < behind) && (10 + 16 * 6 > behind) && (0 == (behind - 10) % 16) )
        {
            const int slot = (behind - 10) / 16;
            int so = -hw / 2;
            if ( slot & 1 )
            {
                so = hw / 2;
            }
            if ( 5 >= abs(o - so) )
            {
                return TK_C_GRID;
            }
        }
        return speckle(h, 15, TK_C_ASPHALT2, TK_C_ASPHALT);
    }
    if ( hw + d->verge >= a )
    {
        for ( int i = 0; i < d->n_features; i++ )
        {
            const tk_feature_t *f = &d->features[i];
            if ( (TK_F_SAND == f->kind) && in_span(f, s, o) )
            {
                return speckle(h, 7, TK_C_SAND2, TK_C_SAND);
            }
        }
        return pick((s / 20) & 1, TK_C_GRASS, TK_C_GRASS2);
    }
    if ( hw + d->verge + WALL_W >= a )
    {
        return pick((s / 5) & 1, TK_C_WALL_R, TK_C_WALL_W);
    }
    if ( hw + d->verge + WALL_W + SHORE_W - 2 >= a )
    {
        return TK_C_SHORE;
    }
    return speckle(h, 7, TK_C_FOREST2, TK_C_FOREST);
}

/** @brief The shortcut: sand stamped through grass and wall alike. */
static void stamp_cut(track_t *t)
{
    const tk_def_t *d = t->def;
    const int r = 9;
    for ( int i = 0; i + 1 < d->n_cut; i++ )
    {
        const float ax = d->cut[i].x, ay = d->cut[i].y, bx = d->cut[i + 1].x, by = d->cut[i + 1].y;
        const int steps = (int)(hypotf(bx - ax, by - ay) * 2.0f) + 1;
        for ( int k = 0; k <= steps; k++ )
        {
            const float px = ax + (bx - ax) * k / steps, py = ay + (by - ay) * k / steps;
            for ( int dy = -r; dy <= r; dy++ )
            {
                for ( int dx = -r; dx <= r; dx++ )
                {
                    if ( r * r < dx * dx + dy * dy )
                    {
                        continue;
                    }
                    const int x = (int)px + dx, y = (int)py + dy;
                    if ( (TK_SIZE <= (unsigned)x) || (TK_SIZE <= (unsigned)y) )
                    {
                        continue;
                    }
                    uint8_t *c = &t->tex[y * TK_SIZE + x];
                    const tk_surface_t s = track_surface_of(*c);
                    if ( (TK_ROAD == s) || (TK_CURB == s) || (TK_BOOST == s) )
                    {
                        continue;
                    }
                    *c = speckle(hash2(x, y), 7, TK_C_SAND2, TK_C_SAND);
                }
            }
        }
    }
}

/** @brief Water in the infield: an ellipse, over the forest only. */
static void lake(track_t *t)
{
    const tk_def_t *d = t->def;
    if ( (!d->lake_r.x) || (!d->lake_r.y) )
    {
        return;
    }
    for ( int y = d->lake.y - d->lake_r.y; d->lake.y + d->lake_r.y >= y; y++ )
    {
        for ( int x = d->lake.x - d->lake_r.x; d->lake.x + d->lake_r.x >= x; x++ )
        {
            if ( (TK_SIZE <= (unsigned)x) || (TK_SIZE <= (unsigned)y) )
            {
                continue;
            }
            const float ex = (float)(x - d->lake.x) / d->lake_r.x, ey = (float)(y - d->lake.y) / d->lake_r.y;
            uint8_t *c = &t->tex[y * TK_SIZE + x];
            if ( (1.0f < ex * ex + ey * ey) || ((TK_C_FOREST != *c) && (TK_C_FOREST2 != *c)) )
            {
                continue;
            }
            /* Ripples: short dashes in rows. */
            *c = pick((0 == (y % 6)) && (5 > ((x + y * 3) % 14)), TK_C_WATER2, TK_C_WATER);
        }
    }
}

/**
 * @brief Things by the track: every DECOR_STEP texels of the line, on one side then the other.
 *
 * They stand on the shore strip behind the wall - never on water or by
 * another stretch. Tyre stacks on the outside of the bends, signs along the
 * start straight, trees and bushes elsewhere.
 */
#define DECOR_STEP 26
static void decor(track_t *t)
{
    const tk_def_t *d = t->def;
    const float base = (float)(d->half_w + d->verge + WALL_W);
    t->n_decor = 0;
    for ( int s = 0; (s < t->length) && (TK_MAX_DECOR > t->n_decor); s += DECOR_STEP )
    {
        const tk_line_t *l = &t->line[s];
        const uint32_t h = hash2(s, 77);
        for ( int side = -1; 1 >= side; side += 2 )
        {
            /* Thin out, unevenly. */
            if ( (0 != (((s / DECOR_STEP) + (0 < side)) % 2)) && (0 != (h & 1)) )
            {
                continue;
            }
            const float out = base + 4.0f + (float)(hash2(s, side) % (SHORE_W - 8));
            const float x = l->x - l->dy * out * side, y = l->y + l->dx * out * side;
            if ( (4 > x) || (4 > y) || (TK_SIZE - 4 <= x) || (TK_SIZE - 4 <= y) )
            {
                continue;
            }
            const uint8_t c = t->tex[(int)y * TK_SIZE + (int)x];
            if ( TK_C_SHORE != c )
            {
                continue;
            }
            /* Not too near another stretch of road (whose wall may be just there). */
            if ( (TK_NO_PROG != t->prog[(int)y * TK_SIZE + (int)x]) &&
                 (base + 4 > abs(t->off[(int)y * TK_SIZE + (int)x])) )
            {
                continue;
            }
            uint8_t kind;
            const bool outside = ((0.012f < l->curv) && (0 > side)) || ((-0.012f > l->curv) && (0 < side));
            if ( outside && (h & 2) )
            {
                kind = TK_D_TYRES;
            }
            else if ( (420 > s) || (t->length - 160 < s) )
            {
                kind = pick(0 == (h >> 3) % 3, TK_D_SIGN, TK_D_TREE);
            }
            else
            {
                kind = pick(0 == (h >> 3) % 4, TK_D_BUSH, TK_D_TREE);
            }
            t->decor[t->n_decor++] = (tk_decor_t){ (int16_t)x, (int16_t)y, kind };
            if ( TK_MAX_DECOR <= t->n_decor )
            {
                break;
            }
        }
    }
}

bool track_build(track_t *t, const tk_def_t *def)
{
    t->def = def;
    if ( !sample_line(t) )
    {
        return false;
    }
    for ( size_t i = 0; (size_t)TK_SIZE * TK_SIZE > i; i++ )
    {
        t->prog[i] = TK_NO_PROG;
    }
    memset(t->off, 0, (size_t)TK_SIZE * TK_SIZE);
    memset(t->tex, 0xFF, (size_t)TK_SIZE * TK_SIZE);
    stamp(t);
    for ( int y = 0; TK_SIZE > y; y++ )
    {
        for ( int x = 0; TK_SIZE > x; x++ )
        {
            const int idx = y * TK_SIZE + x;
            const uint16_t p = t->prog[idx];
            /* Off the band: forest, everywhere. */
            if ( TK_NO_PROG == p )
            {
                t->tex[idx] = speckle(hash2(x, y), 7, TK_C_FOREST2, TK_C_FOREST);
            }
            else
            {
                t->tex[idx] = colour(t, x, y, p, t->off[idx]);
            }
        }
    }
    stamp_cut(t);
    lake(t);
    decor(t);
    return true;
}

tk_surface_t track_surface_of(uint8_t c)
{
    switch ( c )
    {
    case TK_C_ASPHALT: case TK_C_ASPHALT2: case TK_C_LINE: case TK_C_CHECK_B: case TK_C_CHECK_W: case TK_C_GRID:
        return TK_ROAD;
    case TK_C_CURB_R: case TK_C_CURB_W: return TK_CURB;
    case TK_C_GRASS: case TK_C_GRASS2: return TK_GRASS;
    case TK_C_SAND: case TK_C_SAND2: return TK_SAND;
    case TK_C_BOOST: case TK_C_BOOST2: return TK_BOOST;
    case TK_C_ICE: case TK_C_ICE2: return TK_ICE;
    case TK_C_WALL_R: case TK_C_WALL_W: return TK_WALL;
    default: return TK_OUT;
    }
}

tk_surface_t track_surface(const track_t *t, float x, float y)
{
    if ( !((0.0f <= x) && (0.0f <= y) && (TK_SIZE > x) && (TK_SIZE > y)) )
    {
        return TK_OUT;
    }
    return track_surface_of(t->tex[(int)y * TK_SIZE + (int)x]);
}

uint16_t track_prog(const track_t *t, float x, float y)
{
    if ( !((0.0f <= x) && (0.0f <= y) && (TK_SIZE > x) && (TK_SIZE > y)) )
    {
        return TK_NO_PROG;
    }
    return t->prog[(int)y * TK_SIZE + (int)x];
}

int track_offset(const track_t *t, float x, float y)
{
    if ( !((0.0f <= x) && (0.0f <= y) && (TK_SIZE > x) && (TK_SIZE > y)) )
    {
        return 0;
    }
    return t->off[(int)y * TK_SIZE + (int)x];
}

const tk_line_t *track_at(const track_t *t, int s)
{
    s %= t->length;
    if ( 0 > s )
    {
        s += t->length;
    }
    return &t->line[s];
}
