/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file frame_preview.c
 * @brief One frame as the Dot would show it, written to a BMP for eyes.
 *
 * One frame as the Dot would show it (pixels doubled), for eyes, from the
 * centre line at progress s looking along it:
 *     cc main/<every .c but kart_ao, app_main, sound>.c host/frame_preview.c -Imain -lm -o fp && ./fp out.bmp [s] [height] [focal] [back]
 *          [title|menu|pause|results|cup]
 * (Kody stands `back` texels past s, the eye at s; rivals ahead, boxes and seeds, the HUD or a screen.)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hud.h"
#include "race.h"
#include "render.h"
#include "screens.h"
#include "track.h"

static void put32(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    track_t t = { 0 };
    t.tex = malloc(TK_SIZE * TK_SIZE);
    t.prog = malloc(TK_SIZE * TK_SIZE * 2);
    t.off = malloc(TK_SIZE * TK_SIZE);
    t.line = malloc(sizeof(tk_line_t) * TK_MAX_LINE);
    /* KK_CIRCUIT=1 for the beach. */
    const char *which = getenv("KK_CIRCUIT");
    int circuit = 0;
    if ( NULL != which )
    {
        circuit = atoi(which) % TK_CIRCUITS;
    }
    track_build(&t, TK_ALL[circuit]);
    rd_init();
    rd_theme(t.def->theme);
    int s = 3200;
    if ( 2 < argc )
    {
        s = atoi(argv[2]);
    }
    const tk_line_t *l = track_at(&t, s);
    float back = 22.0f;
    if ( 5 < argc )
    {
        back = (float)atof(argv[5]);
    }
    float height = 17.0f;
    if ( 3 < argc )
    {
        height = (float)atof(argv[3]);
    }
    float focal = 110.0f;
    if ( 4 < argc )
    {
        focal = (float)atof(argv[4]);
    }
    rd_camera_t cam = {
        .x = l->x - l->dx * back, .y = l->y - l->dy * back, .angle = atan2f(l->dy, l->dx),
        .height = height, .focal = focal,
    };
    static uint16_t fb[RD_W * RD_H];
    memset(fb, 0, sizeof fb);
    rd_sky(fb, &cam, 0);
    rd_ground(fb, &t, &cam);
    /* A race in progress: rivals strung out ahead, an item in hand, then what the AO draws. */
    static race_t r;
    race_init(&r, &t, RACE_100CC, 5u);
    hud_minimap_build(&t);
    rd_weather(fb, &cam, 12345u);
    r.phase = RACE_RUNNING;
    r.clock = 42.3f;
    r.r[0].lap = 2;
    r.r[0].item = IT_ACORN;
    for ( int i = 0; RACE_KARTS > i; i++ )
    {
        int ahead = 40 + 30 * i;
        float off = (float)((i * 7) % 24 - 12);
        float turn = 0.0f;
        if ( 0 == i )
        {
            ahead = (int)back;
            off = 0.0f;
            turn = 0.25f;
        }
        const tk_line_t *k = track_at(&t, s + ahead);
        kart_place(&r.r[i].k, k->x - k->dy * off, k->y + k->dx * off, atan2f(k->dy, k->dx) + turn);
    }
    for ( int i = RACE_KARTS - 1; 0 <= i; i-- )
    {
        /* Kody drifts on an orange charge with a turbo on; one rival is starred. */
        const rd_kart_fx_t f = { .x = r.r[i].k.x, .y = r.r[i].k.y, .angle = r.r[i].k.angle, .drift = 0 == i,
                                 .charge = 2, .boost = 0 == i, .rough = 3 == i, .star = 2 == i };
        rd_kart_fx(fb, &cam, &f, r.r[i].who, 0);
    }
    for ( int i = 0; i < r.items.n_boxes; i++ )
    {
        rd_billboard(fb, &cam, r.items.boxes[i].x, r.items.boxes[i].y, 1.0f, RD_BOX, 7.0f);
    }
    {
        static const rd_item_sprite_t SP[TK_D_KINDS] = { RD_TREE, RD_BUSH, RD_SIGN, RD_TYRES };
        static const rd_item_sprite_t SP_BEACH[TK_D_KINDS] = { RD_PALM, RD_UMBRELLA, RD_SIGN, RD_TYRES };
        static const rd_item_sprite_t SP_SNOW[TK_D_KINDS] = { RD_PINE, RD_SNOWMAN, RD_SIGN, RD_TYRES };
        static const float W[TK_D_KINDS] = { 14.0f, 9.0f, 16.0f, 6.0f };
        const rd_item_sprite_t *sp = SP;
        if ( TK_THEME_BEACH == t.def->theme )
        {
            sp = SP_BEACH;
        }
        else if ( TK_THEME_SNOW == t.def->theme )
        {
            sp = SP_SNOW;
        }
        /* Far to near, roughly: by distance from the eye. */
        for ( int pass = 0; 5 > pass; pass++ )
        {
            for ( int i = 0; i < t.n_decor; i++ )
            {
                const float dd = hypotf(t.decor[i].x - cam.x, t.decor[i].y - cam.y);
                if ( (4 - pass) != (int)(dd / 120.0f) )
                {
                    continue;
                }
                rd_billboard(fb, &cam, t.decor[i].x, t.decor[i].y, 0.0f, sp[t.decor[i].kind], W[t.decor[i].kind]);
            }
        }
    }
    for ( int i = 0; i < r.items.n_seeds; i++ )
    {
        rd_billboard(fb, &cam, r.items.seeds[i].x, r.items.seeds[i].y, 0.5f, RD_SEED, 3.0f);
    }
    /* argv[6]: a screen over the race instead of the HUD. */
    const char *screen = "race";
    if ( 6 < argc )
    {
        screen = argv[6];
    }
    game_t g;
    game_init(&g);
    if ( 0 == strcmp(screen, "title") )
    {
        screens_title(fb, 0);
    }
    else if ( 0 == strcmp(screen, "menu") )
    {
        records_t rec;
        records_init(&rec);
        rec.best_race_cs[0][0][1] = 7012;
        g.row = GM_ROW_CC;
        screens_menu(fb, &g, &rec, 0);
    }
    else if ( 0 == strcmp(screen, "pause") )
    {
        hud_draw(fb, &r, 0);
        screens_pause(fb);
    }
    else if ( 0 == strcmp(screen, "results") )
    {
        for ( int i = 0; RACE_KARTS > i; i++ )
        {
            if ( 0 == i )
            {
                r.r[i].place = 2;
            }
            else if ( 1 == i )
            {
                r.r[i].place = 1;
            }
            else
            {
                r.r[i].place = (uint8_t)(i + 1);
            }
            r.r[i].finished = 4 > i;
            r.r[i].finish_time = 70.0f + 1.7f * r.r[i].place;
        }
        r.r[0].best_lap = 22.41f;
        screens_results(fb, &r, &g, REC_NEW_RACE, 0);
    }
    else if ( 0 == strcmp(screen, "cup") )
    {
        const uint8_t pts[GM_RACERS] = { 15, 13, 8, 6, 4, 2 };
        for ( int i = 0; GM_RACERS > i; i++ )
        {
            g.points[i] = pts[i];
        }
        screens_cup(fb, &g, 0);
    }
    else
    {
        hud_draw(fb, &r, 0);
    }
    const char *out = "frame.bmp";
    if ( 1 < argc )
    {
        out = argv[1];
    }
    FILE *f = fopen(out, "wb");
    const int W = RD_W * 2, H = RD_H * 2, row = (W * 3 + 3) & ~3;
    fputc('B', f); fputc('M', f); put32(f, 54 + row * H); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, W); put32(f, H); put16(f, 1); put16(f, 24); put32(f, 0); put32(f, row * H);
    put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    for ( int y = H - 1; 0 <= y; y-- )
    {
        int n = 0;
        for ( int x = 0; W > x; x++, n += 3 )
        {
            const uint16_t c = fb[(y / 2) * RD_W + x / 2];
            fputc((c & 31) << 3, f); fputc((c >> 5 & 63) << 2, f); fputc((c >> 11) << 3, f);
        }
        for ( ; n < row; n++ )
        {
            fputc(0, f);
        }
    }
    fclose(f);
    return 0;
}
