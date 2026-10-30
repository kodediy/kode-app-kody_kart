/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file screens.c
 * @brief Title, menu, pause and results, in the HUD's font and colours.
 */
#include "screens.h"

#include <stdbool.h>
#include <stdio.h>

#include "font.h"
#include "hud.h"
#include "palette.h"
#include "render.h"
#include "track.h"

#define SAFE 16

static uint16_t ink(void) { return RGB565(PAL_INK); }
static uint16_t white(void) { return RGB565(PAL_WHITE); }
static uint16_t yellow(void) { return RGB565(PAL_YELLOW); }

/** @brief Darken rows y0..y1 to a third: a panel the words stand out on. */
static void shade(uint16_t *fb, int y0, int y1)
{
    int top = y0;
    if ( 0 > y0 )
    {
        top = 0;
    }
    for ( int y = top; (y1 > y) && (RD_H > y); y++ )
    {
        for ( int x = 0; RD_W > x; x++ )
        {
            const uint16_t c = fb[y * RD_W + x];
            fb[y * RD_W + x] = (uint16_t)(((c >> 2) & 0x39E7) + ((c >> 3) & 0x18E3));
        }
    }
}

void screens_title(uint16_t *fb, uint32_t now_ms)
{
    shade(fb, 40, 132);
    /* KODY in Kody's blue over a yellow KART, the brand's two colours. */
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 48, "KODY", 6, RGB565(PAL_KODY_BLUE), white());
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 48 + 7 * 6 + 6, "KART", 4, yellow(), ink());
    if ( 0u == (now_ms / 500u) % 2u )
    {
        shade(fb, RD_H - SAFE - 26, RD_H - SAFE - 4);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 20, "PRESS TOP", 2, white(), ink());
    }
}

void screens_menu(uint16_t *fb, const game_t *g, const records_t *rec, uint32_t now_ms)
{
    shade(fb, 0, RD_H);
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, SAFE + 4, "KODY KART", 3, yellow(), ink());
    char buf[24];
    static const char *const CC[] = { "50CC", "100CC", "150CC" };
    char cup[20];
    snprintf(cup, sizeof cup, "ALL %d IN TURN", TK_CIRCUITS);
    /* A Grand Prix runs every circuit: the circuit row says so. */
    const char *circuit = cup;
    if ( GM_GRAND_PRIX != g->mode )
    {
        circuit = TK_ALL[g->circuit % TK_CIRCUITS]->name;
    }
    const char *rows[GM_ROWS] = { game_mode_name((gm_mode_t)g->mode), circuit, CC[g->cc % 3], "START!" };
    static const char *const LABEL[GM_ROWS] = { "MODE", "CIRCUIT", "ENGINE", "" };
    for ( int i = 0; GM_ROWS > i; i++ )
    {
        const int y = 58 + i * 34;
        const bool on = i == g->row;
        if ( LABEL[i][0] )
        {
            font_draw_centred(fb, RD_W, RD_H, RD_W / 2, y, LABEL[i], 1, RGB565(PAL_WARM_GRAY), ink());
        }
        if ( on && (GM_ROW_START != i) && (0u == (now_ms / 300u) % 2u) )
        {
            snprintf(buf, sizeof buf, "< %s >", rows[i]);
        }
        else
        {
            snprintf(buf, sizeof buf, "%s", rows[i]);
        }
        uint16_t colour = white();
        if ( on )
        {
            colour = yellow();
        }
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, y + 12, buf, 2, colour, ink());
    }
    /* The record to beat, for this mode and engine. */
    const uint32_t best = rec->best_race_cs[g->circuit % REC_CIRCUITS][game_record_mode(g)][g->cc % REC_CCS];
    if ( best )
    {
        char t[12];
        hud_time(t, sizeof t, (float)best / 100.0f);
        snprintf(buf, sizeof buf, "RECORD %s", t);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 196, buf, 1, yellow(), ink());
    }
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 30, "PAD: CHOOSE   TOP: GO", 1, white(), ink());
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 18, "TOP GAS  UP DRIFT  DOWN BRAKE", 1,
                      RGB565(PAL_WARM_GRAY), ink());
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 8, "BOT ITEM (HOLD: HOME)", 1, RGB565(PAL_WARM_GRAY),
                      ink());
}

void screens_pause(uint16_t *fb)
{
    shade(fb, 0, RD_H);
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 80, "PAUSED", 4, white(), ink());
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 130, "TOP: RACE ON", 2, yellow(), ink());
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 150, "BOT: GIVE UP", 2, white(), ink());
}

void screens_results(uint16_t *fb, const race_t *r, const game_t *g, uint32_t news, uint32_t now_ms)
{
    const bool cup = GM_GRAND_PRIX == g->mode;
    const bool blink = 0u == (now_ms / 250u) % 2u;
    shade(fb, 0, RD_H);
    const racer_t *me = &r->r[0];
    static const char *const ORD[] = { "", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH" };
    char buf[32], t[12];
    if ( 1 == r->n )
    {
        /* A time trial: the clock is the result, then every lap. */
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, SAFE + 2, "TIME TRIAL", 2, white(), ink());
        hud_time(t, sizeof t, me->finish_time);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, SAFE + 22, t, 4, yellow(), ink());
        hud_time(t, sizeof t, me->best_lap);
        snprintf(buf, sizeof buf, "BEST LAP %s", t);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 90, buf, 1, white(), ink());
        if ( (news & REC_NEW_RACE) && blink )
        {
            font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 110, "NEW RECORD!", 2, yellow(), ink());
        }
        else if ( (news & REC_NEW_LAP) && blink )
        {
            font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 110, "NEW BEST LAP!", 2, yellow(), ink());
        }
        if ( 0u == (now_ms / 500u) % 2u )
        {
            font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 26, "TOP: AGAIN", 2, yellow(), ink());
        }
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 8, "BOT: MENU", 1, white(), ink());
        return;
    }
    uint16_t place_colour = white();
    if ( 3 >= me->place )
    {
        place_colour = yellow();
    }
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, SAFE + 2, ORD[me->place], 5, place_colour, ink());
    /* Everyone in their place: a coloured dot, the name, the time (or still racing). */
    int top = 66;
    if ( cup )
    {
        top = 74;
    }
    for ( int p = 1; r->n >= p; p++ )
    {
        for ( int i = 0; r->n > i; i++ )
        {
            const racer_t *x = &r->r[i];
            if ( p != x->place )
            {
                continue;
            }
            const int y = top + (p - 1) * 17;
            const bool mine = 0 == i;
            if ( x->finished )
            {
                hud_time(t, sizeof t, x->finish_time);
            }
            else
            {
                snprintf(t, sizeof t, "--:--.--");
            }
            if ( cup )
            {
                snprintf(buf, sizeof buf, "%d %-5s %s +%d", p, RD_CAST[x->who].name, t, game_points(p));
            }
            else
            {
                snprintf(buf, sizeof buf, "%d %-5s %s", p, RD_CAST[x->who].name, t);
            }
            const uint16_t fur = RGB565(RD_CAST[x->who].fur[1]);
            for ( int dy = 0; 7 > dy; dy++ )
            {
                for ( int dx = 0; 7 > dx; dx++ )
                {
                    fb[(y + dy) * RD_W + SAFE + 4 + dx] = fur;
                }
            }
            uint16_t colour = white();
            if ( mine )
            {
                colour = yellow();
            }
            font_draw(fb, RD_W, RD_H, SAFE + 16, y, buf, 1, colour, ink());
        }
    }
    if ( 0.0f < me->best_lap )
    {
        hud_time(t, sizeof t, me->best_lap);
        snprintf(buf, sizeof buf, "BEST LAP %s", t);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 176, buf, 1, white(), ink());
    }
    if ( (news & REC_NEW_RACE) && blink )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 190, "NEW RECORD!", 2, yellow(), ink());
    }
    else if ( (news & REC_NEW_LAP) && blink )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 190, "NEW BEST LAP!", 2, yellow(), ink());
    }
    if ( cup )
    {
        snprintf(buf, sizeof buf, "GRAND PRIX  RACE %d/%d", g->round + 1, TK_CIRCUITS);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 59, buf, 1, RGB565(PAL_WARM_GRAY), ink());
    }
    const char *next = "TOP: AGAIN";
    if ( cup )
    {
        if ( game_cup_last(g) )
        {
            next = "TOP: STANDINGS";
        }
        else
        {
            next = "TOP: NEXT RACE";
        }
    }
    if ( 0u == (now_ms / 500u) % 2u )
    {
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 26, next, 2, yellow(), ink());
    }
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 8, "BOT: MENU", 1, white(), ink());
}

void screens_cup(uint16_t *fb, const game_t *g, uint32_t now_ms)
{
    shade(fb, 0, RD_H);
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, SAFE + 2, "GRAND PRIX", 3, yellow(), ink());
    /* Order by points; ties keep the racer's number. */
    int order[GM_RACERS];
    for ( int i = 0; GM_RACERS > i; i++ )
    {
        order[i] = i;
    }
    for ( int i = 1; GM_RACERS > i; i++ )
    {
        for ( int j = i; (0 < j) && (g->points[order[j]] > g->points[order[j - 1]]); j-- )
        {
            const int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    }
    char buf[32];
    int mine = 0;
    for ( int p = 0; GM_RACERS > p; p++ )
    {
        const int i = order[p], y = 104 + p * 17;
        if ( 0 == i )
        {
            mine = p + 1;
        }
        snprintf(buf, sizeof buf, "%d %-5s %3d PTS", p + 1, RD_CAST[i].name, g->points[i]);
        const uint16_t fur = RGB565(RD_CAST[i].fur[1]);
        for ( int dy = 0; 7 > dy; dy++ )
        {
            for ( int dx = 0; 7 > dx; dx++ )
            {
                fb[(y + dy) * RD_W + SAFE + 4 + dx] = fur;
            }
        }
        uint16_t colour = white();
        if ( 0 == i )
        {
            colour = yellow();
        }
        font_draw(fb, RD_W, RD_H, SAFE + 16, y, buf, 1, colour, ink());
    }
    /* The trophy: a golden seed for the winner, bobbing. */
    if ( 1 == mine )
    {
        int bob = (int)((now_ms / 120u) % 4u);
        if ( 3 == bob )
        {
            bob = 1;
        }
        rd_icon(fb, RD_W / 2 - 14, 50 + bob, RD_GOLDEN, 4);
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 214, "KODY WINS THE CUP!", 1, yellow(), ink());
    }
    else
    {
        static const char *const ORD[] = { "", "1ST", "2ND", "3RD", "4TH", "5TH", "6TH" };
        snprintf(buf, sizeof buf, "KODY IS %s", ORD[mine]);
        uint16_t colour = white();
        if ( 3 >= mine )
        {
            colour = yellow();
        }
        font_draw_centred(fb, RD_W, RD_H, RD_W / 2, 64, buf, 2, colour, ink());
    }
    font_draw_centred(fb, RD_W, RD_H, RD_W / 2, RD_H - SAFE - 8, "TOP: MENU", 1, white(), ink());
}
