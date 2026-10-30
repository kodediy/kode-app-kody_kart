/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kart_ao.h
 * @brief The Active Object that owns the race and the glass.
 */
#ifndef KART_AO_H_
#define KART_AO_H_

#include <stdbool.h>
#include <stdint.h>

#include "kode_ao.h"

#ifdef __cplusplus
extern "C" {
#endif

extern QActive * const AO_Kart;

/**
 * @brief Construct the kart AO on its initial state, the LED not set yet.
 */
void KartAo_ctor(void);

/**
 * @brief Start the kart AO on its own task, named "kart_ao".
 */
void KartAo_start(void);

/**
 * @brief From the console: start a race of @p mode (gm_mode_t) at @p cc now. Any task.
 * @param mode The race mode, a gm_mode_t.
 * @param cc The engine, 0 to 2 (50cc, 100cc, 150cc).
 * @return false when a start is already pending or could not be posted.
 */
bool kart_ao_start(uint8_t mode, uint8_t cc);

/**
 * @brief From the console: the menu's circuit (index into TK_ALL). Any task.
 * @param circuit The circuit's index into TK_ALL.
 */
void kart_ao_circuit(uint8_t circuit);

#ifdef __cplusplus
}
#endif

#endif /* KART_AO_H_ */
