/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file items.h
 * @brief What lies on the track and what a kart carries: item boxes, seeds,
 *        and the items - turbo, peel, acorn, golden seed.
 *
 * Pure C: no SDK. The boxes and seed rows come from the circuit's
 * TK_F_ITEMS / TK_F_SEEDS spans. A box gives a racer with empty hands a
 * roll (ITEM_ROLL seconds of roulette), then an item weighted by its place:
 * the leader gets things to defend with, the back of the pack things to
 * catch up with.
 */
#ifndef ITEMS_H_
#define ITEMS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { IT_NONE = 0, IT_TURBO, IT_PEEL, IT_ACORN, IT_GOLDEN, IT_COUNT } item_t;

#define ITEM_ROLL      1.0f     /**< seconds of roulette before the item is in hand */
#define ITEM_BOX_BACK  3.0f     /**< seconds until a taken box is back */
#define ITEM_SEED_BACK 10.0f
#define ITEM_MAX_BOXES 16
#define ITEM_MAX_SEEDS 48
#define ITEM_MAX_SHOTS 12
#define ITEM_STAR_TIME 6.0f

typedef struct { float x, y, t; } item_spot_t;       /**< a box or a seed; t > 0: gone for t seconds */

typedef struct {
    uint8_t kind;      /**< IT_PEEL (lies still) or IT_ACORN (rolls along the track); IT_NONE: free */
    uint8_t owner;
    float   x, y, vx, vy;
    float   off;       /**< an acorn's lane: its offset from the centre line */
    int32_t prog;      /**< an acorn's progress round the lap */
    float   age, life;
} item_shot_t;

typedef struct {
    item_spot_t boxes[ITEM_MAX_BOXES];
    uint8_t     n_boxes;
    item_spot_t seeds[ITEM_MAX_SEEDS];
    uint8_t     n_seeds;
    item_shot_t shots[ITEM_MAX_SHOTS];
} items_t;

/** @brief Item events (for the player's racer unless said). */
enum {
    ITEM_EV_BOX    = 1u << 0,   /* took a box: the roulette spins */
    ITEM_EV_GOT    = 1u << 1,   /* the roulette stopped */
    ITEM_EV_USE    = 1u << 2,
    ITEM_EV_HIT    = 1u << 3,   /* the player was hit by a peel or an acorn */
    ITEM_EV_SEED   = 1u << 4,
    ITEM_EV_STRUCK = 1u << 5,   /* the player's shot hit someone */
};

/**
 * @brief The item a racer at @p place of @p n gets for a roll @p u in [0, 1).
 * @param place The racer's place, 1 for the leader.
 * @param n How many racers are in the race.
 * @param u A uniform roll in [0, 1).
 * @return The item it gets.
 */
item_t item_for_place(int place, int n, float u);

#ifdef __cplusplus
}
#endif

#endif /* ITEMS_H_ */
