/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file console.c
 * @brief The race on the cable: the SDK's variables and functions.
 *
 *     kode send 'variable name="summary"'   screen,place/racers,lap/laps,speed
 *     kode send "variable_list"             every row below, with its words
 *     kode send 'function name="race" arg=150cc'     start a single race now (an enum: no quotes)
 *     kode send 'function name="circuit" arg=snow'   ...on Kody Snow
 *
 * Getters run on AO_Console's task and must not block: they read the AO's
 * snapshot. Runners post a static event to the AO and answer at once.
 */
#include "console.h"

#include <stdio.h>

#include "kode.h"

#include "game.h"
#include "kart_ao.h"
#include "items.h"
#include "track.h"

#define TAG "console"

volatile kk_snapshot_t g_kk_snapshot;

static kode_status_t num(char *b, size_t cap, unsigned v) { snprintf(b, cap, "%u", v); return KODE_OK; }
static kode_status_t time_cs(char *b, size_t cap, uint32_t cs)
{
    if ( !cs )
    {
        snprintf(b, cap, "none");
    }
    else
    {
        snprintf(b, cap, "%u:%02u.%02u", (unsigned)(cs / 6000u), (unsigned)(cs / 100u % 60u), (unsigned)(cs % 100u));
    }
    return KODE_OK;
}

static const char *const SCREENS[] = { "title", "menu", "race", "pause", "results" };
static const char *const ITEMS[IT_COUNT] = { "none", "turbo", "peel", "acorn", "golden" };

static kode_status_t get_screen(char *b, size_t c) { snprintf(b, c, "%s", SCREENS[g_kk_snapshot.screen % 5]); return KODE_OK; }
static kode_status_t get_place(char *b, size_t c) { return num(b, c, g_kk_snapshot.place); }
static kode_status_t get_lap(char *b, size_t c) { return num(b, c, g_kk_snapshot.lap); }
static kode_status_t get_speed(char *b, size_t c) { return num(b, c, g_kk_snapshot.speed); }
static kode_status_t get_seeds(char *b, size_t c) { return num(b, c, g_kk_snapshot.seeds); }
static kode_status_t get_item(char *b, size_t c) { snprintf(b, c, "%s", ITEMS[g_kk_snapshot.item % IT_COUNT]); return KODE_OK; }
static kode_status_t get_clock(char *b, size_t c) { return time_cs(b, c, g_kk_snapshot.clock_cs); }
static kode_status_t get_races(char *b, size_t c) { return num(b, c, g_kk_snapshot.races); }
static kode_status_t get_wins(char *b, size_t c) { return num(b, c, g_kk_snapshot.wins); }
static kode_status_t get_podiums(char *b, size_t c) { return num(b, c, g_kk_snapshot.podiums); }
static kode_status_t get_best_race(char *b, size_t c) { return time_cs(b, c, g_kk_snapshot.best_race_cs); }
static kode_status_t get_best_lap(char *b, size_t c) { return time_cs(b, c, g_kk_snapshot.best_lap_cs); }
static kode_status_t get_fps(char *b, size_t c) { return num(b, c, g_kk_snapshot.fps); }

/** @brief Everything in one word: screen,place/racers,lap/laps,speed. */
static kode_status_t get_summary(char *b, size_t c)
{
    snprintf(b, c, "%s,%u/%u,%u/%u,%u", SCREENS[g_kk_snapshot.screen % 5], g_kk_snapshot.place,
             g_kk_snapshot.racers, g_kk_snapshot.lap, g_kk_snapshot.laps, g_kk_snapshot.speed);
    return KODE_OK;
}

static const kode_variable_t VARIABLES[] = {
    { .name = "summary", .get = get_summary, .desc = "screen,place/racers,lap/laps,speed" },
    { .name = "screen", .get = get_screen, .desc = "title menu race pause results" },
    { .name = "place", .get = get_place, .desc = "1-6 in the race" },
    { .name = "lap", .get = get_lap, .desc = "the lap Kody is on" },
    { .name = "speed", .get = get_speed, .desc = "texels a second" },
    { .name = "seeds", .get = get_seeds, .desc = "0-10, +2% top speed each" },
    { .name = "item", .get = get_item, .desc = "none turbo peel acorn golden" },
    { .name = "clock", .get = get_clock, .desc = "the race clock" },
    { .name = "races", .get = get_races, .desc = "races finished, ever" },
    { .name = "wins", .get = get_wins, .desc = "Grand Prix wins" },
    { .name = "podiums", .get = get_podiums, .desc = "Grand Prix top three" },
    { .name = "best_race", .get = get_best_race, .desc = "record, the menu's mode and engine" },
    { .name = "best_lap", .get = get_best_lap, .desc = "best lap, the menu's mode and engine" },
    { .name = "fps", .get = get_fps, .desc = "frames a second on the glass" },
};

static const char *const ENGINES[] = { "50cc", "100cc", "150cc" };

static kode_status_t start(const kode_value_t *a, bool trial)
{
    /* No argument, or one out of range, is 100cc. */
    unsigned cc = 1;
    if ( a && (3 > a->e) )
    {
        cc = a->e;
    }
    gm_mode_t mode = GM_SINGLE;
    if ( trial )
    {
        mode = GM_TIME_TRIAL;
    }
    if ( !kart_ao_start((uint8_t)mode, (uint8_t)cc) )
    {
        return KODE_NO_RESOURCES;
    }
    return KODE_OK;
}
static kode_status_t run_race(const kode_value_t *a) { return start(a, false); }
static kode_status_t run_trial(const kode_value_t *a) { return start(a, true); }

static const char *const CIRCUITS[] = { "park", "beach", "snow" };
_Static_assert(TK_CIRCUITS == sizeof CIRCUITS / sizeof CIRCUITS[0], "a word per circuit");

static kode_status_t run_circuit(const kode_value_t *a)
{
    if ( !a ) return KODE_DENIED;
    kart_ao_circuit(a->e);
    return KODE_OK;
}

static const kode_function_t FUNCTIONS[] = {
    { .name = "circuit", .run = run_circuit, .arg = KODE_TYPE_ENUM, .enums = CIRCUITS, .n_enums = TK_CIRCUITS,
      .desc = "The next race's circuit: arg=snow" },
    { .name = "race", .run = run_race, .arg = KODE_TYPE_ENUM, .enums = ENGINES, .n_enums = 3,
      .desc = "A single race now: arg=100cc (no quotes)" },
    { .name = "trial", .run = run_trial, .arg = KODE_TYPE_ENUM, .enums = ENGINES, .n_enums = 3,
      .desc = "A time trial now: arg=150cc (no quotes)" },
};

void console_start(void)
{
    if ( KODE_OK != KODE_VARIABLES_ADD(VARIABLES) ) KLOG_E(TAG, "variables refused");
    if ( KODE_OK != KODE_FUNCTIONS_ADD(FUNCTIONS) ) KLOG_E(TAG, "functions refused");
}
