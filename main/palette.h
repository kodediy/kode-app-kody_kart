/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file palette.h
 * @brief The colours: the ground's (one per TK_C_* index) and the brand's.
 *
 * Pure C. The ground palette is RGB888 here and RGB565 at draw time; the
 * host preview reads the same table, so what the preview shows is the glass.
 */
#ifndef PALETTE_H_
#define PALETTE_H_

#include <stdint.h>

#include "track.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t PAL_GROUND[TK_C_COUNT];

/**
 * @brief A ground colour in a circuit's theme: the park is PAL_GROUND, the beach repaints a few.
 * @param theme A tk_theme_t.
 * @param c     A TK_C_* index.
 * @return The colour, RGB888.
 */
uint32_t pal_ground(int theme, int c);

#define RGB565(rgb) ((uint16_t)((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F)))

/* The brand (packages/brand/tokens.css). */
#define PAL_KODY_BLUE 0x028AC2
#define PAL_YELLOW    0xFFAC00
#define PAL_INK       0x191919
#define PAL_WARM_GRAY 0xDED7CD
#define PAL_RED       0xC9303D
#define PAL_WHITE     0xFFFFFF

#ifdef __cplusplus
}
#endif

#endif /* PALETTE_H_ */
