/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file sound.h
 * @brief The race's sounds: one tone at a time, tunes queued note by note.
 *
 * kode_audio_tone() refuses while the codec is busy, so a tune is a queue the
 * frame steps through (kodegotchi's way). A new sound replaces the queue only
 * if it matters at least as much as what is playing.
 */
#ifndef SOUND_H_
#define SOUND_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How much a sound matters: a finish beats a bump. */
typedef enum { SND_LOW = 0, SND_MID, SND_HIGH } sound_prio_t;

/**
 * @brief Queue @p notes pairs of (hz, ms) - hz 0 is a rest - at @p prio.
 * @param hz_ms The tune: 2 * notes values, hz then ms for each note.
 * @param notes How many notes; more than the queue holds are dropped.
 * @param prio How much it matters; a lower one does not cut in on what plays.
 */
void sound_play(const uint16_t *hz_ms, int notes, sound_prio_t prio);

/**
 * @brief Play the next note when its time has come. Call every frame.
 * @param now_ms The time now, in milliseconds.
 */
void sound_step(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H_ */
