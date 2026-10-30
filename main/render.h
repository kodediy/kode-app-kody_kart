/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file render.h
 * @brief The picture: a logical RD_W x RD_H RGB565 frame.
 *
 * Pure C: no SDK. The race fills the glass (the sky above RD_HORIZON, the
 * Mode 7 ground below it); the HUD and the minimap are drawn over it. The
 * adapter doubles every pixel onto the glass.
 */
#ifndef RENDER_H_
#define RENDER_H_

#include <stdbool.h>
#include <stdint.h>

#include "track.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RD_W       205
#define RD_H       251
#define RD_VIEW_H  RD_H    /**< rows of race: all of them */
#define RD_HORIZON 78      /**< the first ground row: a third down the portrait glass */

/** @brief Where the eye is: on the ground at (x, y), looking along angle, raised height. */
typedef struct {
    float x, y;
    float angle;    /**< radians; 0 looks along +x, pi/2 along +y (the world is y-down) */
    float height;   /**< texels above the ground */
    float focal;    /**< logical pixels: the field of view is 2 atan(RD_W / 2 / focal) */
} rd_camera_t;

/** @brief Build the RGB565 tables (ground x fog). Call once. */
void rd_init(void);

/**
 * @brief Repaint the ground tables for a circuit's theme (tk_theme_t).
 * @param theme The circuit's theme, a tk_theme_t.
 */
void rd_theme(int theme);

/**
 * @brief The theme's weather over the finished frame: falling snow on the
 *        snow circuit; nothing elsewhere. Also steps the water's shimmer
 *        (a palette swap, no cost per pixel). Call once a frame.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param cam    The eye the frame was drawn from.
 * @param now_ms Milliseconds now, for the flakes' fall and the shimmer.
 */
void rd_weather(uint16_t *fb, const rd_camera_t *cam, uint32_t now_ms);

/**
 * @brief The sky over the ground, rows 0 .. RD_HORIZON - 1: hills turn with the eye, clouds drift.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param cam    The eye: its angle turns the hills.
 * @param now_ms Milliseconds now, for the clouds' drift.
 */
void rd_sky(uint16_t *fb, const rd_camera_t *cam, uint32_t now_ms);

/**
 * @brief The Mode 7 ground, rows RD_HORIZON .. RD_VIEW_H - 1.
 * @param fb  The logical RD_W x RD_H RGB565 frame.
 * @param t   The circuit whose texture is the ground.
 * @param cam The eye.
 */
void rd_ground(uint16_t *fb, const track_t *t, const rd_camera_t *cam);

/** @brief A character: the kart's three tones and the driver's fur's three, RGB888 light/base/dark. */
typedef struct {
    const char *name;
    uint32_t    body[3];
    uint32_t    fur[3];
} rd_character_t;

#define RD_CHARACTERS 6
extern const rd_character_t RD_CAST[RD_CHARACTERS];   /**< 0 is Kody */

/** @brief How many sprite pixels a texel of the world is, unscaled: the kart model is ~8 texels long. */
#define RD_SPRITE_PER_TEXEL 3.0f

/**
 * @brief A kart at world (x, y) whose nose points along @p angle, lifted
 *        @p lift texels (a hop), seen by @p cam. @p who indexes RD_CAST.
 * @param fb    The logical RD_W x RD_H RGB565 frame.
 * @param cam   The eye.
 * @param x     World x of the kart, in texels.
 * @param y     World y of the kart, in texels.
 * @param angle Where the nose points, in radians.
 * @param lift  Texels above the ground (a hop).
 * @param who   The character, an index into RD_CAST.
 */
void rd_kart(uint16_t *fb, const rd_camera_t *cam, float x, float y, float angle, float lift, int who);

/** @brief What is happening to a kart, for its effects. */
typedef struct {
    float   x, y, angle, lift;
    int8_t  drift;       /**< 0, -1, +1 */
    uint8_t charge;      /**< kart_charge_level(): 0, 1 blue, 2 orange */
    bool    boost;       /**< a turbo is on: flames */
    bool    rough;       /**< on grass or sand at speed: dust */
    bool    star;        /**< invincible: the kart flashes gold */
    bool    dizzy;       /**< spun out: stars circle over the driver */
} rd_kart_fx_t;

/**
 * @brief A ghost kart: pale, every other pixel, so the track shows through.
 * @param fb    The logical RD_W x RD_H RGB565 frame.
 * @param cam   The eye.
 * @param x     World x of the kart, in texels.
 * @param y     World y of the kart, in texels.
 * @param angle Where the nose points, in radians.
 */
void rd_kart_ghost(uint16_t *fb, const rd_camera_t *cam, float x, float y, float angle);

/**
 * @brief The kart with its effects: sparks, flames, dust; a star flashes it gold.
 * @param fb     The logical RD_W x RD_H RGB565 frame.
 * @param cam    The eye.
 * @param k      Where the kart is and what is happening to it.
 * @param who    The character, an index into RD_CAST.
 * @param now_ms Milliseconds now, for the effects' flicker.
 */
void rd_kart_fx(uint16_t *fb, const rd_camera_t *cam, const rd_kart_fx_t *k, int who, uint32_t now_ms);

/** @brief What a billboard shows (the item sprites). */
typedef enum {
    RD_BOX = 0, RD_SEED, RD_GOLDEN, RD_PEEL, RD_ACORN, RD_TURBO,
    RD_TREE, RD_BUSH, RD_SIGN, RD_TYRES, RD_PALM, RD_UMBRELLA, RD_PINE, RD_SNOWMAN, RD_ITEM_SPRITES
} rd_item_sprite_t;

/**
 * @brief An item sprite standing on the ground at (x, y), @p wide texels
 *        across, lifted @p lift texels.
 * @param fb   The logical RD_W x RD_H RGB565 frame.
 * @param cam  The eye.
 * @param x    World x of the sprite's foot, in texels.
 * @param y    World y of the sprite's foot, in texels.
 * @param lift Texels above the ground.
 * @param what The sprite.
 * @param wide Its width in texels.
 */
void rd_billboard(uint16_t *fb, const rd_camera_t *cam, float x, float y, float lift, rd_item_sprite_t what, float wide);

/**
 * @brief An item sprite flat on the screen, top-left at (x, y), each pixel @p scale x @p scale.
 * @param fb    The logical RD_W x RD_H RGB565 frame.
 * @param x     Left column.
 * @param y     Top row.
 * @param what  The sprite.
 * @param scale Logical pixels per sprite pixel.
 */
void rd_icon(uint16_t *fb, int x, int y, rd_item_sprite_t what, int scale);

/**
 * @brief The icon's size in sprite pixels.
 * @param what The sprite.
 * @param w    Out: its width.
 * @param h    Out: its height.
 */
void rd_icon_size(rd_item_sprite_t what, int *w, int *h);

/**
 * @brief Project a ground point to the screen.
 * @param cam   The eye.
 * @param wx    World x of the point, in texels.
 * @param wy    World y of the point, in texels.
 * @param sx    Out: its screen column.
 * @param sy    Out: its ground row.
 * @param scale Out: logical pixels per texel there.
 * @return false when it is behind the eye or too close; else its column,
 *         ground row, and scale (1.0 = a texel is a logical pixel).
 */
int rd_project(const rd_camera_t *cam, float wx, float wy, float *sx, float *sy, float *scale);

#ifdef __cplusplus
}
#endif

#endif /* RENDER_H_ */
