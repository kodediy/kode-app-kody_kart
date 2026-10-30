/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file console.h
 * @brief The race on the cable: SDK variables and functions (`kode send`).
 */
#ifndef CONSOLE_H_
#define CONSOLE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the console reads: a copy the AO refreshes every frame (bytes and words only). */
typedef struct {
    uint8_t  screen, mode, cc;
    uint8_t  place, racers, lap, laps, seeds, item;
    uint16_t speed;          /**< texels a second */
    uint32_t clock_cs;       /**< race clock, centiseconds */
    uint16_t races, wins, podiums;
    uint32_t best_race_cs, best_lap_cs;   /**< for the menu's mode and engine */
    uint8_t  fps;
} kk_snapshot_t;

/** @brief The AO's latest snapshot (console.c reads it). */
extern volatile kk_snapshot_t g_kk_snapshot;

/**
 * @brief Register the variables and functions. After kode_init().
 */
void console_start(void);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H_ */
