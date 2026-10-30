/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ai.h
 * @brief A rival's brain: where to steer, when to brake, how fast it may go.
 *
 * Pure C: no SDK. It reads the race (its own kart, the circuit, the player's
 * progress) and writes an input and its stats for this step, the same input a
 * person's buttons make.
 */
#ifndef AI_H_
#define AI_H_

#include "kart.h"
#include "race.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Drive racer @p i for @p dt seconds: fill @p in, and adjust @p st (skill, rubber band).
 * @param r  The race.
 * @param i  The racer to drive (index into the race).
 * @param dt Seconds of this step.
 * @param in Filled with this step's input.
 * @param st Scaled by the rival's skill and the rubber band.
 */
void ai_drive(race_t *r, int i, float dt, kart_input_t *in, kart_stats_t *st);

#ifdef __cplusplus
}
#endif

#endif /* AI_H_ */
