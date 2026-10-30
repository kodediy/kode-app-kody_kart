/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file game.h
 * @brief The screens around the race: title, menu, race, pause, results.
 *
 * Pure C: no SDK. Buttons go in (gm_button_t, presses only), an action comes
 * out for the adapter to carry out (start a race, go home). What the menu
 * holds - the mode and the engine - lives here.
 */
#ifndef GAME_H_
#define GAME_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { GM_TITLE = 0, GM_MENU, GM_RACE, GM_PAUSE, GM_RESULTS, GM_CUP } gm_screen_t;
/** @brief A Grand Prix is every circuit in turn, for points; a single race is the menu's circuit. */
typedef enum { GM_GRAND_PRIX = 0, GM_SINGLE, GM_TIME_TRIAL, GM_MODES } gm_mode_t;

#define GM_RACERS 6   /**< RACE_KARTS: the cup keeps everyone's points */
typedef enum { GM_ROW_MODE = 0, GM_ROW_CIRCUIT, GM_ROW_CC, GM_ROW_START, GM_ROWS } gm_row_t;
typedef enum { GM_B_UP = 0, GM_B_DOWN, GM_B_LEFT, GM_B_RIGHT, GM_B_TOP, GM_B_BOT, GM_B_TOUCH } gm_button_t;

/** @brief What the adapter must do after a press. */
typedef enum {
    GM_NONE = 0,
    GM_START,       /* a race with the menu's mode and engine */
    GM_RESUME,
    GM_QUIT_RACE,   /* back to the menu, the race abandoned */
    GM_TICK,        /* a menu move: a click */
    GM_CUP_OVER,    /* the Grand Prix's standings: a fanfare */
} gm_action_t;

typedef struct {
    uint8_t screen;   /* gm_screen_t */
    uint8_t mode;     /* gm_mode_t */
    uint8_t cc;       /* race_cc_t */
    uint8_t circuit;  /* index into TK_ALL */
    uint8_t row;      /* gm_row_t: the menu's highlighted row */
    uint8_t round;    /* the Grand Prix's race: 0 .. TK_CIRCUITS - 1 */
    uint8_t points[GM_RACERS];   /* the Grand Prix so far, by racer */
} game_t;

/**
 * @brief Points for a place in a Grand Prix race.
 * @param place The place, 1 for the winner.
 * @return The points; 0 for a place out of range.
 */
int game_points(int place);

/**
 * @brief Start at the title screen with the menu's defaults.
 * @param g The game's state.
 */
void game_init(game_t *g);

/**
 * @brief A button press on the current screen.
 * @param g The game's state.
 * @param b The button pressed.
 * @return What the adapter must do.
 */
gm_action_t game_press(game_t *g, gm_button_t b);

/**
 * @brief The race reached its end (the player crossed the line for good).
 * @param g      The game's state.
 * @param place  Every racer's place, @p racers of them (a Grand Prix scores them).
 * @param racers How many racers @p place holds.
 */
void game_race_over(game_t *g, const uint8_t *place, int racers);

/**
 * @brief Which records a mode files under: 0 races, 1 time trials.
 * @param g The game's state.
 * @return 0 for races, 1 for time trials.
 */
int game_record_mode(const game_t *g);

/**
 * @brief Whether this is the Grand Prix's last race.
 * @param g The game's state.
 * @return Non-zero on the last round.
 */
int game_cup_last(const game_t *g);

/**
 * @brief The mode's name, in capitals, for the menu.
 * @param m The mode.
 * @return A constant string.
 */
const char *game_mode_name(gm_mode_t m);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H_ */
