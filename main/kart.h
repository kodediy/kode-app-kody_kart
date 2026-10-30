/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kart.h
 * @brief One kart's physics: throttle, steering, the drift and its turbo, surfaces and walls.
 *
 * Pure C: no SDK. A kart steps by a time in seconds with an input (a person's
 * buttons or a rival's brain, the same struct) over a built track. Speeds are
 * texels a second; angles radians in a y-down world (0 = +x, pi/2 = +y).
 *
 * The input is the throttle (held: TOP, the Dot's "A"), steering, the brake
 * (which reverses once stopped) and the hop button. Off the throttle the
 * kart coasts down. The hop held while steering is a drift: the kart slides round a tighter or
 * wider arc as you steer into or out of it, and charges a mini-turbo (blue at
 * KART_DRIFT_BLUE seconds, orange at KART_DRIFT_ORANGE) that fires when the
 * button is let go.
 */
#ifndef KART_H_
#define KART_H_

#include <stdbool.h>
#include <stdint.h>

#include "track.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KART_DRIFT_BLUE   0.7f
#define KART_DRIFT_ORANGE 1.5f
#define KART_SEEDS_MAX    10
#define KART_RADIUS       4.0f    /**< texels: walls and other karts */

/** @brief What drives a kart this step. */
typedef struct {
    float steer;    /**< -1 full left .. +1 full right */
    bool  gas;      /**< the throttle */
    bool  brake;
    bool  hop;      /**< held: a hop, then a drift while steering */
    bool  use;      /**< use the item (an edge: set for one step) */
} kart_input_t;

/** @brief How a kart drives: a person by the cc, a rival by its skill. */
typedef struct {
    float top;      /**< top speed on asphalt */
    float accel;    /**< texels/s^2 below the top */
    float turn;     /**< rad/s at full steer and speed */
} kart_stats_t;

/** @brief Something that happened this step, for sound, buzz and the LED. */
enum {
    KART_EV_WALL      = 1u << 0,
    KART_EV_DRIFT     = 1u << 1,   /* a drift began */
    KART_EV_BLUE      = 1u << 2,   /* the charge reached blue */
    KART_EV_ORANGE    = 1u << 3,   /* ...and orange */
    KART_EV_MINITURBO = 1u << 4,   /* a charged drift was let go */
    KART_EV_PAD       = 1u << 5,   /* drove over a boost pad */
    KART_EV_HOP       = 1u << 6,
};

typedef struct {
    float   x, y;
    float   angle;      /**< where the nose points */
    float   speed;      /**< along the nose; negative reversing */
    float   vx, vy;     /**< where it actually goes: lags the nose in a slide */
    float   steer;      /**< smoothed input */
    int8_t  drift;      /**< 0, or -1 / +1: drifting left / right */
    float   charge;     /**< seconds into the drift */
    float   boost;      /**< seconds of turbo left */
    float   boost_k;    /**< how strong it is (a mini-turbo less than a pad) */
    float   hop;        /**< seconds of hop left (the sprite's jump) */
    float   spin;       /**< seconds of spin-out left: no control */
    float   star;       /**< seconds of invincibility left */
    uint8_t seeds;      /**< 0..KART_SEEDS_MAX: +2% top speed each */
    uint8_t surface;    /**< tk_surface_t under the kart */
    bool    hop_held;   /**< last step's hop, for the edge */
} kart_t;

/**
 * @brief A kart at rest at (x, y) facing @p angle.
 * @param k     The kart to reset.
 * @param x     World x, texels.
 * @param y     World y, texels.
 * @param angle Where the nose points, radians.
 */
void kart_place(kart_t *k, float x, float y, float angle);

/**
 * @brief Advance @p k by @p dt seconds.
 * @param k  The kart.
 * @param st How it drives.
 * @param in What drives it this step.
 * @param t  The track it drives on.
 * @param dt Seconds of this step.
 * @return KART_EV_* bits.
 */
uint32_t kart_step(kart_t *k, const kart_stats_t *st, const kart_input_t *in, const track_t *t, float dt);

/**
 * @brief Spin out (a peel, an acorn): no control for a moment, a seed or two lost.
 * @param k The kart hit.
 */
void kart_hit(kart_t *k);

/**
 * @brief Start a turbo of @p seconds at strength @p strength (1 = a pad).
 * @param k        The kart.
 * @param seconds  How long the turbo lasts.
 * @param strength How strong it is (1 = a pad).
 */
void kart_boost(kart_t *k, float seconds, float strength);

/**
 * @brief The drift's colour now: 0 none, 1 blue, 2 orange.
 * @param k The kart.
 * @return 0, 1 or 2.
 */
int kart_charge_level(const kart_t *k);

#ifdef __cplusplus
}
#endif

#endif /* KART_H_ */
