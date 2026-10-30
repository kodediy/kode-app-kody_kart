/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file race.h
 * @brief A race: the grid, the countdown, laps, places, the finish.
 *
 * Pure C: no SDK. A race owns RACE_KARTS racers on one built track; racer 0
 * is the person (Kody), the rest are rivals driven by ai.c. Every racer's
 * progress is UNWRAPPED texels round the circuit (lap * length + s), read off
 * the track's progress map each step: it starts negative on the grid, crosses
 * 0 on the line, and the race is done at laps * length. A step that would
 * move it more than RACE_MAX_JUMP is ignored, so a kart can only gain
 * progress by driving it (the dip's shortcut skips less than that).
 */
#ifndef RACE_H_
#define RACE_H_

#include <stdbool.h>
#include <stdint.h>

#include "items.h"
#include "kart.h"
#include "track.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RACE_KARTS      6
#define RACE_MAX_JUMP   300
#define RACE_COUNTDOWN  3.0f    /**< seconds of 3, 2, 1 before GO */

typedef enum { RACE_COUNTING = 0, RACE_RUNNING, RACE_DONE } race_phase_t;

/** @brief Difficulty: everyone's engine. */
typedef enum { RACE_50CC = 0, RACE_100CC, RACE_150CC } race_cc_t;

typedef struct {
    kart_t   k;
    uint8_t  who;          /**< RD_CAST index: the colours */
    bool     cpu;
    int32_t  prog;         /**< unwrapped texels round the circuit */
    int16_t  lap;          /**< laps begun: 0 on the grid, 1 after the first crossing */
    uint8_t  place;        /**< 1..RACE_KARTS */
    float    place_at;     /**< the clock when the place last changed */
    bool     finished;
    float    finish_time;
    float    lap_t0, last_lap, best_lap;
    float    wrong;        /**< seconds driving against the circuit */
    /* The rival's brain (ai.c). */
    float    skill;        /**< 0.96 .. 1.04 of the player's engine */
    float    lane;         /**< its preferred offset from the racing line, texels */
    float    lane_t;       /**< seconds until it picks another */
    float    stuck;        /**< seconds crawling: past a second it backs off */
    uint32_t rng;
    /* What it carries. */
    uint8_t  item;         /**< item_t in hand */
    uint8_t  stock;        /**< how many of it are left (a time trial's three turbos); 0 counts as 1 */
    float    roll;         /**< seconds of roulette left; the item lands when it ends */
    float    item_t;       /**< a rival: how long it has held the item */
    float    gas_from;     /**< the countdown: the clock when the gas went down and stayed; 0 = up */
} racer_t;

typedef struct {
    const track_t *t;
    racer_t  r[RACE_KARTS];
    uint8_t  laps;
    uint8_t  cc;           /**< race_cc_t */
    uint8_t  phase;        /**< race_phase_t */
    float    clock;        /**< seconds since GO (negative while counting) */
    uint8_t  finishers;
    uint8_t  n;            /**< racers in this race: RACE_KARTS, or 1 in a time trial */
    uint32_t rng;
    items_t  items;
} race_t;

/** @brief What happened in a step (for the player unless noted). */
enum {
    RACE_EV_COUNT     = 1u << 0,   /* a countdown number went by */
    RACE_EV_GO        = 1u << 1,
    RACE_EV_LAP       = 1u << 2,   /* a new lap began */
    RACE_EV_FINAL_LAP = 1u << 3,
    RACE_EV_FINISH    = 1u << 4,
    RACE_EV_OVERTAKE  = 1u << 5,   /* the player gained a place */
    RACE_EV_OVERTAKEN = 1u << 6,
    RACE_EV_BUMP      = 1u << 7,   /* karts touched */
    RACE_EV_BEST_LAP  = 1u << 8,
    RACE_EV_ROCKET    = 1u << 9,   /* a perfect start: a turbo off the line */
    RACE_EV_STALL     = 1u << 10,  /* the gas held too long: the engine floods */
};

/**
 * @brief The stats of a kart at a difficulty.
 * @param cc The difficulty (race_cc_t).
 * @return The kart's engine, grip and speed figures for that difficulty.
 */
kart_stats_t race_stats(race_cc_t cc);

/**
 * @brief A new race on a track: everyone on the grid, the player last.
 * @param r The race to fill.
 * @param t The built track to race on.
 * @param cc The difficulty.
 * @param seed The seed of the race's random numbers.
 */
void race_init(race_t *r, const track_t *t, race_cc_t cc, uint32_t seed);

/**
 * @brief Advance by @p dt seconds: the countdown, every kart, laps and places.
 * @param r The race.
 * @param in The player's input (its `use` fires the item).
 * @param dt Seconds to advance.
 * @param kart_ev Out: OR of the player's KART_EV_*.
 * @param item_ev Out: the ITEM_EV_* bits.
 * @return RACE_EV_* bits.
 */
uint32_t race_step(race_t *r, const kart_input_t *in, float dt, uint32_t *kart_ev, uint32_t *item_ev);

/* ---- items (items.c) ---------------------------------------------------- */

/**
 * @brief Lay the circuit's boxes and seeds out. race_init() calls it.
 * @param r The race whose track holds the item spans.
 */
void race_items_init(race_t *r);

/**
 * @brief Boxes, seeds, rolls and shots for @p dt.
 * @param r The race.
 * @param dt Seconds to advance.
 * @return ITEM_EV_* bits.
 */
uint32_t race_items_step(race_t *r, float dt);

/**
 * @brief Racer @p i uses what it holds.
 * @param r The race.
 * @param i The racer's index.
 * @return ITEM_EV_USE when it had something.
 */
uint32_t race_use_item(race_t *r, int i);

/**
 * @brief Make @p r a time trial: the player alone, no boxes or seeds, three turbos. After race_init().
 * @param r The race to turn into a time trial.
 */
void race_time_trial(race_t *r);

/* The start: gas pressed in this window of the countdown (seconds before GO) is a rocket start;
 * held from earlier than ROCKET_EARLY floods the engine. */
#define ROCKET_FROM  1.6f
#define ROCKET_TO    0.5f
#define ROCKET_EARLY 2.2f

/**
 * @brief The countdown's number now (3, 2, 1), or 0 once it is over.
 * @param r The race.
 * @return 3, 2 or 1 while counting; 0 once it is over.
 */
int race_count(const race_t *r);

/**
 * @brief Whether the player is driving the wrong way (for long enough to say so).
 * @param r The race.
 * @return true when the player has driven the wrong way long enough.
 */
bool race_wrong_way(const race_t *r);

/**
 * @brief Unwrapped progress at the kart's position, for an observer (a minimap).
 * @param r The race.
 * @param racer The racer's index.
 * @return The racer's unwrapped progress in texels.
 */
int race_lap_of(const race_t *r, int racer);

#ifdef __cplusplus
}
#endif

#endif /* RACE_H_ */
