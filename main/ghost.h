/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ghost.h
 * @brief A time trial's ghost: a race recorded as positions, played back at the same clock.
 *
 * Pure C: no SDK. GHOST_HZ samples a second, a texel/8 position and a byte
 * of heading each, enough for 3 laps of any circuit (GHOST_MAX). Playback
 * interpolates between samples.
 */
#ifndef GHOST_H_
#define GHOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GHOST_HZ   15
#define GHOST_MAX  2400      /**< 160 s at GHOST_HZ */
#define GHOST_VERSION 1

typedef struct {
    int16_t x, y;            /**< texels x 8 */
    uint8_t a;               /**< heading, 256ths of a turn */
} ghost_sample_t;

typedef struct {
    uint16_t version;
    uint8_t  circuit, cc;
    uint16_t n;              /**< samples kept */
    uint32_t time_cs;        /**< the race it recorded; 0 = empty */
    ghost_sample_t s[GHOST_MAX];
} ghost_t;

/** @brief Bytes worth saving: the header and the samples kept, not the room after them. */
#define GHOST_BYTES(g) (offsetof(ghost_t, s) + (size_t)(g)->n * sizeof(ghost_sample_t))

/**
 * @brief Read @p circuit's saved ghost into @p g.
 * @param g Where the ghost lands; left empty when there is none.
 * @param circuit The circuit whose ghost to read.
 * @return false when there is none.
 */
bool ghost_load(ghost_t *g, int circuit);

/**
 * @brief Save @p g as its circuit's ghost.
 * @param g The ghost to save: its header and the samples it kept.
 * @return false when the write failed.
 */
bool ghost_save(const ghost_t *g);

/**
 * @brief An empty ghost for @p circuit at @p cc.
 * @param g The ghost to reset.
 * @param circuit The circuit it records.
 * @param cc The engine it records at.
 */
void ghost_start(ghost_t *g, int circuit, int cc);

/**
 * @brief Record the kart at race clock @p t (seconds): kept when a sample is due.
 * @param g The ghost being recorded.
 * @param t The race clock, in seconds.
 * @param x The kart's x, in texels.
 * @param y The kart's y, in texels.
 * @param angle The kart's heading, in radians.
 */
void ghost_add(ghost_t *g, float t, float x, float y, float angle);

/**
 * @brief Where the ghost is at @p t.
 * @param g The ghost to play back.
 * @param t The race clock, in seconds.
 * @param x Out: its x, in texels.
 * @param y Out: its y, in texels.
 * @param angle Out: its heading, in radians.
 * @return false before its first or after its last sample.
 */
bool ghost_at(const ghost_t *g, float t, float *x, float *y, float *angle);

#ifdef __cplusplus
}
#endif

#endif /* GHOST_H_ */
