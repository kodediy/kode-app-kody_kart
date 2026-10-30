/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hud.c
 * @brief hud_draw(): the place top left, the lap and time top right, big words in the middle.
 */
#include "hud.h"

#include <stdbool.h>
#include <stdio.h>

#include "font.h"
#include "palette.h"
#include "render.h"

#define MAP_N    64                    /* the minimap is MAP_N x MAP_N pixels */
#define MAP_DIV  (TK_SIZE / MAP_N)     /* world texels per minimap pixel */
/* The Dot's bezel is thick and its corners round: nothing the player must read
 * goes nearer the edge than this (logical pixels; twice that on the glass). */
#define SAFE     16

#define MAP_X    SAFE
#define MAP_Y    (RD_H - MAP_N - SAFE)

/* What each minimap pixel is: 0 nothing, 1 road, 2 the road's edge, 3 the line. */
static uint8_t s_map[MAP_N * MAP_N];

void hud_minimap_build(const track_t *t)
{
    for ( int my = 0; MAP_N > my; my++ )
    {
        for ( int mx = 0; MAP_N > mx; mx++ )
        {
            /* Road if a fair share of the cell is road. */
            int road = 0, line = 0;
            for ( int y = my * MAP_DIV; (my + 1) * MAP_DIV > y; y += 2 )
            {
                for ( int x = mx * MAP_DIV; (mx + 1) * MAP_DIV > x; x += 2 )
                {
                    const uint8_t c = track_texel(t, x, y);
                    const tk_surface_t s = track_surface_of(c);
                    road += (TK_ROAD == s) || (TK_CURB == s) || (TK_BOOST == s);
                    line += (TK_C_CHECK_B == c) || (TK_C_CHECK_W == c);
                }
            }
            uint8_t m = 0;
            if ( line )
            {
                m = 3;
            }
            else if ( (MAP_DIV / 2) * (MAP_DIV / 2) <= road * 4 )
            {
                m = 1;
            }
            s_map[my * MAP_N + mx] = m;
        }
    }
    /* The edge: empty cells next to road. */
    for ( int my = 0; MAP_N > my; my++ )
    {
        for ( int mx = 0; MAP_N > mx; mx++ )
        {
            if ( s_map[my * MAP_N + mx] )
            {
                continue;
            }
            for ( int d = 0; 4 > d; d++ )
            {
                const int x = mx + (0 == d) - (1 == d), y = my + (2 == d) - (3 == d);
                if ( (MAP_N > (unsigned)x) && (MAP_N > (unsigned)y) && (s_map[y * MAP_N + x] & 1) )
                {
                    s_map[my * MAP_N + mx] = 2;
                    break;
                }
            }
        }
    }
}

static void dot(uint16_t *fb, int x, int y, int r, uint16_t c, uint16_t ring)
{
    for ( int dy = -r - 1; r + 1 >= dy; dy++ )
    {
        for ( int dx = -r - 1; r + 1 >= dx; dx++ )
        {
            const int X = x + dx, Y = y + dy, d2 = dx * dx + dy * dy;
            if ( (0 > X) || (0 > Y) || (RD_W <= X) || (RD_H <= Y) || ((r + 1) * (r + 1) < d2) )
            {
                continue;
            }
            if ( r * r >= d2 )
            {
                fb[Y * RD_W + X] = c;
            }
            else
            {
                fb[Y * RD_W + X] = ring;
            }
        }
    }
}

static void minimap(uint16_t *fb, const race_t *r, uint32_t now_ms)
{
    const uint16_t road = RGB565(0xE8E8EC), edge = RGB565(PAL_INK), line = RGB565(PAL_WHITE);
    for ( int my = 0; MAP_N > my; my++ )
    {
        for ( int mx = 0; MAP_N > mx; mx++ )
        {
            const uint8_t m = s_map[my * MAP_N + mx];
            if ( !m )
            {
                continue;
            }
            uint16_t c = line;
            if ( 1 == m )
            {
                c = road;
            }
            else if ( 2 == m )
            {
                c = edge;
            }
            fb[(MAP_Y + my) * RD_W + MAP_X + mx] = c;
        }
    }
    /* Rivals first, Kody on top, blinking. */
    for ( int i = r->n - 1; 0 <= i; i-- )
    {
        const racer_t *x = &r->r[i];
        if ( (0 == i) && (0u == (now_ms / 250u) % 4u) )
        {
            continue;
        }
        const int px = MAP_X + (int)(x->k.x / MAP_DIV), py = MAP_Y + (int)(x->k.y / MAP_DIV);
        /* Kody's dot is bigger and ringed in white. */
        int radius = 1;
        uint32_t ring = PAL_INK;
        if ( 0 == i )
        {
            radius = 2;
            ring = PAL_WHITE;
        }
        dot(fb, px, py, radius, RGB565(RD_CAST[x->who].fur[1]), RGB565(ring));
    }
}

/** @brief The item in hand: a framed slot top centre; the roulette spins through the icons. */
static void item_slot(uint16_t *fb, const race_t *r, uint32_t now_ms)
{
    static const rd_item_sprite_t ICON[IT_COUNT] = {
        [IT_TURBO] = RD_TURBO, [IT_PEEL] = RD_PEEL, [IT_ACORN] = RD_ACORN, [IT_GOLDEN] = RD_GOLDEN,
    };
    const int S = 30, x0 = RD_W / 2 - S / 2, y0 = SAFE - 2;
    const uint16_t ink = RGB565(PAL_INK), rim = RGB565(PAL_WHITE), back = RGB565(0x2A2A30);
    for ( int y = 0; S > y; y++ )
    {
        for ( int x = 0; S > x; x++ )
        {
            const bool edge = (2 > x) || (2 > y) || (S - 2 <= x) || (S - 2 <= y);
            const bool corner = ((2 > x) || (S - 2 <= x)) && ((2 > y) || (S - 2 <= y));
            if ( corner )
            {
                continue;
            }
            uint16_t c = back;
            if ( edge )
            {
                /* The outermost ring is ink, the one inside it the rim. */
                if ( (0 == x) || (0 == y) || (S - 1 == x) || (S - 1 == y) )
                {
                    c = ink;
                }
                else
                {
                    c = rim;
                }
            }
            fb[(y0 + y) * RD_W + x0 + x] = c;
        }
    }
    const racer_t *me = &r->r[0];
    if ( IT_NONE == me->item )
    {
        return;
    }
    item_t shown = (item_t)me->item;
    if ( 0.0f < me->roll )
    {
        shown = (item_t)(IT_TURBO + (now_ms / 70u) % (IT_COUNT - 1));
    }
    int w, h;
    rd_icon_size(ICON[shown], &w, &h);
    int sc = 1;
    if ( (S - 6 >= w * 2) && (S - 6 >= h * 2) )
    {
        sc = 2;
    }
    rd_icon(fb, x0 + (S - w * sc) / 2, y0 + (S - h * sc) / 2, ICON[shown], sc);
    if ( 1 < me->stock )
    {
        char n[8];
        snprintf(n, sizeof n, "x%d", me->stock);
        font_draw(fb, RD_W, RD_H, x0 + S + 3, y0 + S - 8, n, 1, RGB565(PAL_WHITE), RGB565(PAL_INK));
    }
}

static const char *ordinal(int n)
{
    switch ( n )
    {
    case 1: return "ST";
    case 2: return "ND";
    case 3: return "RD";
    default: return "TH";
    }
}

void hud_time(char *out, size_t cap, float seconds)
{
    if ( !(0.0f < seconds) )
    {
        seconds = 0.0f;   /* NaN too */
    }
    unsigned cs = (unsigned)(seconds * 100.0f + 0.5f);
    if ( 59999u < cs )
    {
        cs = 59999u;
    }
    snprintf(out, cap, "%u:%02u.%02u", (cs / 6000u) % 10u, (cs / 100u) % 60u, cs % 100u);
}

void hud_draw(uint16_t *fb, const race_t *r, uint32_t now_ms)
{
    const uint16_t ink = RGB565(PAL_INK), white = RGB565(PAL_WHITE), yellow = RGB565(PAL_YELLOW);
    const racer_t *me = &r->r[0];
    char buf[24];

    /* The place: big, yellow when on the podium; it flashes for a moment when it changes. */
    snprintf(buf, sizeof buf, "%d", me->place);
    const bool fresh = (RACE_RUNNING == r->phase) && (0.6f > r->clock - me->place_at) && (1 < r->n);
    uint16_t pc = white;
    uint16_t place_ring = ink;
    if ( fresh )
    {
        place_ring = RGB565(PAL_KODY_BLUE);
    }
    if ( !(fresh && ((now_ms / 80u) % 2u)) && (3 >= me->place) )
    {
        pc = yellow;
    }
    font_draw(fb, RD_W, RD_H, SAFE, SAFE, buf, 4, pc, place_ring);
    font_draw(fb, RD_W, RD_H, SAFE + font_width(buf, 4) + 2, SAFE, ordinal(me->place), 2, pc, ink);

    /* The lap and the race clock. */
    int lap = me->lap;
    if ( 1 > me->lap )
    {
        lap = 1;
    }
    else if ( r->laps < me->lap )
    {
        lap = r->laps;
    }
    snprintf(buf, sizeof buf, "LAP %d/%d", lap, r->laps);
    font_draw(fb, RD_W, RD_H, RD_W - SAFE - font_width(buf, 1), SAFE + 1, buf, 1, white, ink);
    if ( me->finished )
    {
        hud_time(buf, sizeof buf, me->finish_time);
    }
    else
    {
        hud_time(buf, sizeof buf, r->clock);
    }
    font_draw(fb, RD_W, RD_H, RD_W - SAFE - font_width(buf, 1), SAFE + 11, buf, 1, white, ink);

    minimap(fb, r, now_ms);
    item_slot(fb, r, now_ms);

    /* Seeds, bottom right: gold once full. */
    snprintf(buf, sizeof buf, "x%d", me->k.seeds);
    const int sw = font_width(buf, 2), sy = RD_H - SAFE - 14;
    rd_item_sprite_t seed_icon = RD_SEED;
    uint16_t seed_colour = white;
    if ( KART_SEEDS_MAX <= me->k.seeds )
    {
        seed_icon = RD_GOLDEN;
        seed_colour = yellow;
    }
    rd_icon(fb, RD_W - SAFE - sw - 18, sy - 2, seed_icon, 2);
    font_draw(fb, RD_W, RD_H, RD_W - SAFE - sw, sy, buf, 2, seed_colour, ink);

    /* The countdown, then GO! for a second. */
    const int count = race_count(r);
    if ( 0 < count )
    {
        snprintf(buf, sizeof buf, "%d", count);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 56, buf, 7, white, ink);
        /* How to drive, while there is time to read it. */
        if ( !me->cpu )
        {
            font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 114, "HOLD TOP = GAS", 2, yellow, ink);
            font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 134, "UP+TURN=DRIFT  BOT=ITEM", 1, white, ink);
        }
    }
    else if ( (RACE_RUNNING == r->phase) && (1.0f > r->clock) )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 56, "GO!", 6, yellow, ink);
    }

    /* The signs. */
    if ( race_wrong_way(r) && (0u == (now_ms / 300u) % 2u) )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 64, "WRONG WAY", 2, RGB565(PAL_RED), white);
    }
    if ( (r->laps == me->lap) && (1 < r->laps) && (2.5f > r->clock - me->lap_t0) && !me->finished )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 64, "FINAL LAP", 3, yellow, ink);
    }
    /* The lap just done, for two seconds after the line. */
    if ( (2 <= me->lap) && !me->finished && (2.0f > r->clock - me->lap_t0) && (0.0f < me->last_lap) )
    {
        char t[12];
        hud_time(t, sizeof t, me->last_lap);
        snprintf(buf, sizeof buf, "LAP %s", t);
        const bool best = me->best_lap + 1e-3f >= me->last_lap;
        /* On the final lap the FINAL LAP sign holds row 64: go lower. */
        int row = 64;
        if ( r->laps == me->lap )
        {
            row = 92;
        }
        uint16_t colour = white;
        if ( best )
        {
            colour = yellow;
        }
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, row, buf, 2, colour, ink);
    }
    if ( me->finished )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 56, "FINISH!", 4, white, ink);
    }
}
