/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file game.c
 * @brief game_press(): which screen a press leads to.
 *
 * Title: TOP opens the menu. Menu: UP/DOWN move between rows, LEFT/RIGHT
 * change the mode or the engine, TOP on START (or anywhere) starts. Race: a
 * touch pauses (BOT is the item there). Pause: TOP resumes, BOT gives up.
 * Results: TOP races again (in a Grand Prix: the next circuit, then the
 * cup's standings), BOT goes to the menu. Cup: any press, the menu.
 */
#include "game.h"

#include <string.h>

#include "race.h"
#include "track.h"

void game_init(game_t *g)
{
    *g = (game_t){ .screen = GM_TITLE, .mode = GM_GRAND_PRIX, .cc = RACE_100CC, .row = GM_ROW_START };
}

const char *game_mode_name(gm_mode_t m)
{
    if ( GM_TIME_TRIAL == m )
    {
        return "TIME TRIAL";
    }
    if ( GM_SINGLE == m )
    {
        return "SINGLE RACE";
    }
    return "GRAND PRIX";
}

int game_points(int place)
{
    static const uint8_t P[GM_RACERS + 1] = { 0, 9, 6, 4, 3, 2, 1 };
    if ( (1 <= place) && (GM_RACERS >= place) )
    {
        return P[place];
    }
    return 0;
}

int game_record_mode(const game_t *g)
{
    if ( GM_TIME_TRIAL == g->mode )
    {
        return 1;
    }
    return 0;
}

int game_cup_last(const game_t *g)
{
    return TK_CIRCUITS <= g->round + 1;
}

/** @brief The menu's START: a Grand Prix begins at its first circuit with no points. */
static gm_action_t start(game_t *g)
{
    if ( GM_GRAND_PRIX == g->mode )
    {
        g->round = 0;
        g->circuit = 0;
        memset(g->points, 0, sizeof g->points);
    }
    g->screen = GM_RACE;
    return GM_START;
}

static gm_action_t menu_press(game_t *g, gm_button_t b)
{
    switch ( b )
    {
    case GM_B_UP: g->row = (uint8_t)((g->row + GM_ROWS - 1) % GM_ROWS); return GM_TICK;
    case GM_B_DOWN: g->row = (uint8_t)((g->row + 1) % GM_ROWS); return GM_TICK;
    case GM_B_LEFT:
    case GM_B_RIGHT: {
        int d = -1;
        if ( GM_B_RIGHT == b )
        {
            d = 1;
        }
        if ( GM_ROW_MODE == g->row )
        {
            g->mode = (uint8_t)((g->mode + GM_MODES + d) % GM_MODES);
        }
        else if ( GM_ROW_CC == g->row )
        {
            g->cc = (uint8_t)((g->cc + 3 + d) % 3);
        }
        else if ( GM_ROW_CIRCUIT == g->row )
        {
            g->circuit = (uint8_t)((g->circuit + TK_CIRCUITS + d) % TK_CIRCUITS);
        }
        else
        {
            return GM_NONE;
        }
        return GM_TICK;
    }
    case GM_B_TOP:
    case GM_B_TOUCH:
        return start(g);
    case GM_B_BOT:
        g->screen = GM_TITLE;
        return GM_TICK;
    default:
        return GM_NONE;
    }
}

gm_action_t game_press(game_t *g, gm_button_t b)
{
    switch ( g->screen )
    {
    case GM_TITLE:
        if ( (GM_B_TOP == b) || (GM_B_TOUCH == b) )
        {
            g->screen = GM_MENU;
            return GM_TICK;
        }
        return GM_NONE;
    case GM_MENU:
        return menu_press(g, b);
    case GM_RACE:
        if ( GM_B_TOUCH == b )
        {
            g->screen = GM_PAUSE;
            return GM_TICK;
        }
        return GM_NONE;
    case GM_PAUSE:
        if ( (GM_B_TOP == b) || (GM_B_TOUCH == b) )
        {
            g->screen = GM_RACE;
            return GM_RESUME;
        }
        if ( GM_B_BOT == b )
        {
            g->screen = GM_MENU;
            return GM_QUIT_RACE;
        }
        return GM_NONE;
    case GM_RESULTS:
        if ( (GM_B_TOP == b) && (GM_GRAND_PRIX == g->mode) )
        {
            if ( game_cup_last(g) )
            {
                g->screen = GM_CUP;
                return GM_CUP_OVER;
            }
            g->round++;
            g->circuit = g->round;
            g->screen = GM_RACE;
            return GM_START;
        }
        if ( GM_B_TOP == b )
        {
            g->screen = GM_RACE;
            return GM_START;
        }
        if ( GM_B_BOT == b )
        {
            g->screen = GM_MENU;
            return GM_QUIT_RACE;
        }
        return GM_NONE;
    case GM_CUP:
        if ( (GM_B_TOP == b) || (GM_B_BOT == b) || (GM_B_TOUCH == b) )
        {
            g->screen = GM_MENU;
            return GM_QUIT_RACE;
        }
        return GM_NONE;
    default:
        return GM_NONE;
    }
}

void game_race_over(game_t *g, const uint8_t *place, int racers)
{
    if ( (GM_RACE != g->screen) && (GM_PAUSE != g->screen) )
    {
        return;
    }
    g->screen = GM_RESULTS;
    if ( GM_GRAND_PRIX != g->mode )
    {
        return;
    }
    for ( int i = 0; (racers > i) && (GM_RACERS > i); i++ )
    {
        g->points[i] = (uint8_t)(g->points[i] + game_points(place[i]));
    }
}
