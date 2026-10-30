/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_game.c
 * @brief Host checks for game.c: the screens and the menu. Prints "OK <n>".
 */
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "race.h"
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

int main(void)
{
    game_t g;
    game_init(&g);
    CHECK(GM_TITLE == g.screen);
    CHECK(game_press(&g, GM_B_LEFT) == GM_NONE && GM_TITLE == g.screen);
    CHECK(game_press(&g, GM_B_TOP) == GM_TICK && GM_MENU == g.screen);

    /* The menu: rows wrap, left/right change what the row holds. */
    CHECK(GM_ROW_START == g.row);
    game_press(&g, GM_B_DOWN);
    CHECK(GM_ROW_MODE == g.row);
    game_press(&g, GM_B_RIGHT);
    CHECK(GM_SINGLE == g.mode);
    game_press(&g, GM_B_RIGHT);
    CHECK(GM_TIME_TRIAL == g.mode && game_record_mode(&g) == 1);
    game_press(&g, GM_B_RIGHT);
    CHECK(GM_GRAND_PRIX == g.mode && game_record_mode(&g) == 0);
    game_press(&g, GM_B_RIGHT);   /* a single race, for the flow below */
    game_press(&g, GM_B_DOWN);
    CHECK(GM_ROW_CIRCUIT == g.row && 0 == g.circuit);
    for ( int c = 1; TK_CIRCUITS >= c; c++ )
    {
        game_press(&g, GM_B_RIGHT);
        CHECK(c % TK_CIRCUITS == g.circuit);   /* round them all and back to the first */
    }
    game_press(&g, GM_B_LEFT);
    CHECK(TK_CIRCUITS - 1 == g.circuit);
    game_press(&g, GM_B_DOWN);
    CHECK(GM_ROW_CC == g.row && RACE_100CC == g.cc);
    game_press(&g, GM_B_RIGHT);
    CHECK(RACE_150CC == g.cc);
    game_press(&g, GM_B_RIGHT);
    CHECK(RACE_50CC == g.cc);
    game_press(&g, GM_B_LEFT);
    CHECK(RACE_150CC == g.cc);
    CHECK(game_press(&g, GM_B_TOP) == GM_START && GM_RACE == g.screen);

    /* Racing: buttons drive, a touch pauses; from the pause, TOP resumes, BOT quits. */
    CHECK(game_press(&g, GM_B_TOP) == GM_NONE);
    CHECK(game_press(&g, GM_B_BOT) == GM_NONE);
    CHECK(game_press(&g, GM_B_TOUCH) == GM_TICK && GM_PAUSE == g.screen);
    CHECK(game_press(&g, GM_B_TOP) == GM_RESUME && GM_RACE == g.screen);
    game_press(&g, GM_B_TOUCH);
    CHECK(game_press(&g, GM_B_BOT) == GM_QUIT_RACE && GM_MENU == g.screen);

    /* The end of a single race: results, then again or the menu; no points. */
    const uint8_t places[GM_RACERS] = { 2, 1, 3, 4, 5, 6 };
    game_press(&g, GM_B_TOP);
    game_race_over(&g, places, GM_RACERS);
    CHECK(GM_RESULTS == g.screen && 0 == g.points[0]);
    CHECK(game_press(&g, GM_B_TOP) == GM_START && GM_RACE == g.screen);
    game_race_over(&g, places, GM_RACERS);
    CHECK(game_press(&g, GM_B_BOT) == GM_QUIT_RACE && GM_MENU == g.screen);
    game_race_over(&g, places, GM_RACERS);   /* not racing: nothing */
    CHECK(GM_MENU == g.screen);

    /* A Grand Prix: every circuit in turn, points each time, then the cup. */
    g.mode = GM_GRAND_PRIX;
    g.circuit = 1;
    g.points[3] = 99;
    CHECK(game_press(&g, GM_B_TOP) == GM_START);
    CHECK(0 == g.circuit && 0 == g.round && 0 == g.points[3]);
    for ( int race = 0; TK_CIRCUITS > race; race++ )
    {
        CHECK(g.circuit == race);
        game_race_over(&g, places, GM_RACERS);
        CHECK(GM_RESULTS == g.screen);
        const gm_action_t a = game_press(&g, GM_B_TOP);
        if ( race + 1 < TK_CIRCUITS ) CHECK(GM_START == a && GM_RACE == g.screen);
        else CHECK(GM_CUP_OVER == a && GM_CUP == g.screen);
    }
    CHECK(6 * TK_CIRCUITS == g.points[0] && 9 * TK_CIRCUITS == g.points[1] && 1 * TK_CIRCUITS == g.points[5]);
    CHECK(game_press(&g, GM_B_TOP) == GM_QUIT_RACE && GM_MENU == g.screen);
    printf("OK %d\n", n_ok);
    return 0;
}
