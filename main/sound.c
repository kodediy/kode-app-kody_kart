/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file sound.c
 * @brief The note queue over kode_audio_tone().
 */
#include "sound.h"

#include "kode.h"

#define MAX_NOTES 12
#define VOLUME    40
#define GAP_MS    25

static struct { uint16_t hz, ms; } s_q[MAX_NOTES];
static uint8_t  s_n, s_i, s_prio;
static uint32_t s_at;
static bool     s_due;
static uint32_t s_retry_until;   /* a refused note is tried again until then */

#define RETRY_MS 150

void sound_play(const uint16_t *hz_ms, int notes, sound_prio_t prio)
{
    if ( (s_i < s_n) && (prio < s_prio) ) return;   /* something more important is playing */
    if ( MAX_NOTES < notes ) notes = MAX_NOTES;
    for ( int i = 0; i < notes; i++ )
    {
        s_q[i].hz = hz_ms[2 * i];
        s_q[i].ms = hz_ms[2 * i + 1];
    }
    s_n = (uint8_t)notes;
    s_i = 0;
    s_prio = (uint8_t)prio;
    s_due = true;
}

void sound_step(uint32_t now_ms)
{
    if ( s_n <= s_i ) return;
    if ( !s_due && (0 > (int32_t)(now_ms - s_at)) ) return;
    if ( s_due ) s_retry_until = now_ms + RETRY_MS;
    s_due = false;
    /* The codec closes a moment after a tone and refuses the next meanwhile: try again next frame. */
    if ( s_q[s_i].hz && (KODE_OK != kode_audio_tone(s_q[s_i].hz, s_q[s_i].ms, VOLUME)) &&
        (0 > (int32_t)(now_ms - s_retry_until)) )
    {
        s_at = now_ms;
        return;
    }
    s_at = now_ms + s_q[s_i].ms + GAP_MS;
    s_retry_until = s_at + RETRY_MS;
    s_i++;
}
