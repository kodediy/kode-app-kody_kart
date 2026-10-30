/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file hud.h
 * @brief What is written over the race: place, lap, time, the countdown, signs.
 *
 * Pure C: no SDK. Reads the race; draws into the logical frame.
 */
#ifndef HUD_H_
#define HUD_H_

#include <stddef.h>
#include <stdint.h>

#include "race.h"
#include "track.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The HUD over the race, @p now_ms for blinking.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param r      The race to report.
 * @param now_ms Milliseconds now, for blinking.
 */
void hud_draw(uint16_t *fb, const race_t *r, uint32_t now_ms);

/**
 * @brief Shrink the circuit into the minimap. Call once per track.
 * @param t The circuit.
 */
void hud_minimap_build(const track_t *t);

/**
 * @brief "1:23.45" (at most 9:59.99).
 * @param out     Where the text goes.
 * @param cap     The size of @p out in bytes.
 * @param seconds The time to write.
 */
void hud_time(char *out, size_t cap, float seconds);

#ifdef __cplusplus
}
#endif

#endif /* HUD_H_ */
