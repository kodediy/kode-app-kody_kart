/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_render.c
 * @brief Render smoke: every drawing path, on both circuits, from awkward places.
 *
 * Render smoke: every drawing path, on both circuits, from awkward places,
 * into a frame with guard words after it. The sanitizer and the guards catch
 * any write outside the frame. Prints "OK <n>".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hud.h"
#include "race.h"
#include "records.h"
#include "render.h"
#include "screens.h"

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

#define GUARD 256
static uint16_t frame[RD_W * RD_H + GUARD];

static void guards_set(void) { for ( int i = 0; GUARD > i; i++ ) frame[RD_W * RD_H + i] = 0xA5A5; }
static void guards_check(void) { for ( int i = 0; GUARD > i; i++ ) CHECK(0xA5A5 == frame[RD_W * RD_H + i]); }

int main(void)
{
    static track_t t;
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    rd_init();
    static race_t r;
    static game_t g;
    static records_t rec;
    records_init(&rec);
    game_init(&g);
    for ( int c = 0; TK_CIRCUITS > c; c++ )
    {
        CHECK(track_build(&t, TK_ALL[c]));
        rd_theme(t.def->theme);
        hud_minimap_build(&t);
        race_init(&r, &t, RACE_150CC, 77u);
        for ( int s = 0; s < t.length; s += 97 )
        {
            const tk_line_t *l = track_at(&t, s);
            rd_camera_t cam = { .x = l->x, .y = l->y, .angle = atan2f(l->dy, l->dx) + (float)(s % 7) * 0.3f,
                                .height = 17.0f, .focal = 88.0f + (float)(s % 3) * 11.0f };
            guards_set();
            rd_sky(frame, &cam, (uint32_t)s * 33u);
            rd_ground(frame, &t, &cam);
            /* A kart right at the eye, one far off, one beside: all clipped. */
            for ( int k = 0; RACE_KARTS > k; k++ )
            {
                float d = 30.0f * (float)k;
                if ( 0 == k )
                {
                    d = 4.5f;
                }
                const rd_kart_fx_t f = { .x = cam.x + cosf(cam.angle) * d + (float)(k - 3) * 9.0f,
                                         .y = cam.y + sinf(cam.angle) * d, .angle = (float)k, .lift = (float)(k & 1) * 2.0f,
                                         .drift = (int8_t)(k % 3 - 1), .charge = (uint8_t)(k % 3), .boost = k & 1,
                                         .rough = k & 2, .star = 4 == k, .dizzy = 2 == k };
                rd_kart_fx(frame, &cam, &f, k, (uint32_t)s);
            }
            rd_kart_ghost(frame, &cam, cam.x + cosf(cam.angle) * 5.0f, cam.y + sinf(cam.angle) * 5.0f, 1.0f);
            for ( int w = 0; RD_ITEM_SPRITES > w; w++ )
                rd_billboard(frame, &cam, cam.x + cosf(cam.angle) * (4.2f + 3.0f * w), cam.y + sinf(cam.angle) * 4.2f - 20.0f + 4.0f * w, 3.0f, (rd_item_sprite_t)w, 16.0f);
            for ( int i = 0; i < t.n_decor; i++ )
            {
                static const rd_item_sprite_t SP[TK_D_KINDS] = { RD_TREE, RD_BUSH, RD_SIGN, RD_TYRES };
                rd_billboard(frame, &cam, t.decor[i].x, t.decor[i].y, 0.0f, SP[t.decor[i].kind], 14.0f);
            }
            rd_weather(frame, &cam, (uint32_t)s * 517u);
            /* Every screen over it, in turn. */
            r.r[0].item = (uint8_t)(s % IT_COUNT);
            if ( 0 != (s % 2) )
            {
                r.r[0].roll = 0.5f;
            }
            else
            {
                r.r[0].roll = 0.0f;
            }
            r.r[0].k.seeds = (uint8_t)(s % 11);
            r.phase = (uint8_t)(s % 3);
            r.clock = (float)(s % 200) - 3.0f;
            for ( int k = 0; RACE_KARTS > k; k++ ) r.r[k].k.x = (float)((s * 13 + k * 101) % 1100) - 40.0f;
            switch ( s % 6 )
            {
            case 0: hud_draw(frame, &r, (uint32_t)s); break;
            case 1: screens_title(frame, (uint32_t)s); break;
            case 2: screens_menu(frame, &g, &rec, (uint32_t)s); break;
            case 3: screens_pause(frame); break;
            case 4: screens_results(frame, &r, &g, REC_NEW_LAP, (uint32_t)s); break;
            default: screens_cup(frame, &g, (uint32_t)s); break;
            }
            guards_check();
        }
        /* Off the world, looking out of it. */
        rd_camera_t out = { .x = -300.0f, .y = 1500.0f, .angle = 2.0f, .height = 17.0f, .focal = 110.0f };
        guards_set();
        rd_sky(frame, &out, 0);
        rd_ground(frame, &t, &out);
        guards_check();
    }
    printf("OK %d\n", n_ok);
    return 0;
}
