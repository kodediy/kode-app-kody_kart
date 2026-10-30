/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file screens.h
 * @brief The screens drawn over the race picture: title, menu, pause, results.
 *
 * Pure C: no SDK. Each draws into the logical frame on top of whatever the
 * race camera painted (the title and the menu over a demo race).
 */
#ifndef SCREENS_H_
#define SCREENS_H_

#include <stdint.h>

#include "game.h"
#include "race.h"
#include "records.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The title: KODY KART on a dark panel and a blinking prompt.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param now_ms Milliseconds now, for blinking.
 */
void screens_title(uint16_t *fb, uint32_t now_ms);

/**
 * @brief The menu: mode, circuit, engine and START, with the circuit's records.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param g      The menu's state.
 * @param rec    The records to show.
 * @param now_ms Milliseconds now, for blinking.
 */
void screens_menu(uint16_t *fb, const game_t *g, const records_t *rec, uint32_t now_ms);

/**
 * @brief The pause panel.
 * @param fb The logical RD_W x RD_H RGB565 frame.
 */
void screens_pause(uint16_t *fb);

/**
 * @brief The Grand Prix's standings by points.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param g      The Grand Prix's state.
 * @param now_ms Milliseconds now, for blinking.
 */
void screens_cup(uint16_t *fb, const game_t *g, uint32_t now_ms);

/**
 * @brief The results of a race: places, times, new records.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param r      The finished race.
 * @param g      The game's state (mode, Grand Prix round).
 * @param news   REC_NEW_* from this race.
 * @param now_ms Milliseconds now, for blinking.
 */
void screens_results(uint16_t *fb, const race_t *r, const game_t *g, uint32_t news, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SCREENS_H_ */
