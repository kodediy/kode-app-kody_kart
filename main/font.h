/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file font.h
 * @brief A 5x7 pixel font, drawn at any whole scale with an ink outline.
 *
 * Pure C: no SDK. Digits, capitals and . : ! / - ' x % # + = ? (the HUD's needs);
 * lower case is drawn as capitals; anything else is a space.
 */
#ifndef FONT_H_
#define FONT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT_W 5
#define FONT_H 7

/**
 * @brief Width in pixels of @p text at @p scale (one column of space between letters).
 * @param text  The text.
 * @param scale Pixels per font pixel.
 * @return Its width in pixels.
 */
int font_width(const char *text, int scale);

/**
 * @brief Draw @p text with its top-left at (x, y) into a @p fb_w x @p fb_h
 *        RGB565 frame, in @p colour with a one-pixel @p outline (or none when
 *        outline == colour).
 * @param fb      The RGB565 frame.
 * @param fb_w    Its width in pixels.
 * @param fb_h    Its height in pixels.
 * @param x       Left column of the text.
 * @param y       Top row of the text.
 * @param text    The text.
 * @param scale   Pixels per font pixel.
 * @param colour  The letters' colour.
 * @param outline The outline's colour (equal to @p colour for none).
 */
void font_draw(uint16_t *fb, int fb_w, int fb_h, int x, int y, const char *text, int scale, uint16_t colour,
               uint16_t outline);

/**
 * @brief The same as font_draw(), centred on column @p cx.
 * @param fb      The RGB565 frame.
 * @param fb_w    Its width in pixels.
 * @param fb_h    Its height in pixels.
 * @param cx      The column the text is centred on.
 * @param y       Top row of the text.
 * @param text    The text.
 * @param scale   Pixels per font pixel.
 * @param colour  The letters' colour.
 * @param outline The outline's colour (equal to @p colour for none).
 */
void font_draw_centred(uint16_t *fb, int fb_w, int fb_h, int cx, int y, const char *text, int scale, uint16_t colour,
                       uint16_t outline);

#ifdef __cplusplus
}
#endif

#endif /* FONT_H_ */
