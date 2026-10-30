/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file items.c
 * @brief Boxes, seeds, the roulette, using an item, and the shots in flight.
 */
#include "items.h"

#include <math.h>
#include <string.h>

#include "race.h"

#define BOX_SPACING   11      /* texels between boxes in a row */
#define SEED_SPACING  16      /* texels between seeds along a row */
#define PICK_R        7.0f    /* how close a kart must come to take a box or a seed */
#define HIT_R         6.0f
#define ACORN_SPEED   260.0f
#define ACORN_LIFE    5.0f
#define PEEL_LIFE     45.0f
#define SAFE_AGE      0.4f    /* a shot cannot hit its owner this soon */

item_t item_for_place(int place, int n, float u)
{
    /* Three bands: the front, the middle, the back. Weights in percent. */
    static const uint8_t W[3][IT_COUNT] = {
        /*            none turbo peel acorn golden */
        /* front  */ { 0, 10, 60, 30, 0 },
        /* middle */ { 0, 30, 25, 35, 10 },
        /* back   */ { 0, 40, 0, 30, 30 },
    };
    int band = 1;
    if ( 1 >= place )
    {
        band = 0;
    }
    else if ( (n - 1) <= place )
    {
        band = 2;
    }
    int roll = (int)(u * 100.0f);
    for ( int it = IT_TURBO; IT_COUNT > it; it++ )
    {
        if ( roll < W[band][it] ) return (item_t)it;
        roll -= W[band][it];
    }
    return IT_TURBO;
}

static uint32_t rng_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static float rng_unit(uint32_t *s) { return (float)(rng_next(s) % 10000u) / 10000.0f; }

void race_items_init(race_t *r)
{
    items_t *it = &r->items;
    memset(it, 0, sizeof *it);
    const tk_def_t *d = r->t->def;
    for ( int f = 0; f < d->n_features; f++ )
    {
        const tk_feature_t *ft = &d->features[f];
        if ( TK_F_ITEMS == ft->kind )
        {
            const tk_line_t *l = track_at(r->t, ft->s0);
            for ( int o = ft->o0; o <= ft->o1 && ITEM_MAX_BOXES > it->n_boxes; o += BOX_SPACING )
            {
                item_spot_t *b = &it->boxes[it->n_boxes++];
                b->x = l->x - l->dy * (float)o;
                b->y = l->y + l->dx * (float)o;
            }
        }
        else if ( TK_F_SEEDS == ft->kind )
        {
            for ( int s = ft->s0; s < ft->s1 && ITEM_MAX_SEEDS > it->n_seeds; s += SEED_SPACING )
            {
                const tk_line_t *l = track_at(r->t, s);
                item_spot_t *sd = &it->seeds[it->n_seeds++];
                sd->x = l->x - l->dy * (float)ft->o0;
                sd->y = l->y + l->dx * (float)ft->o0;
            }
        }
    }
}

static bool near(float ax, float ay, float bx, float by, float r)
{
    const float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy < r * r;
}

static item_shot_t *free_shot(items_t *it)
{
    for ( int i = 0; ITEM_MAX_SHOTS > i; i++ )
        if ( IT_NONE == it->shots[i].kind ) return &it->shots[i];
    /* All in use: the oldest peel makes way. */
    item_shot_t *old = &it->shots[0];
    for ( int i = 1; ITEM_MAX_SHOTS > i; i++ )
        if ( it->shots[i].age > old->age ) old = &it->shots[i];
    return old;
}

uint32_t race_use_item(race_t *r, int i)
{
    racer_t *x = &r->r[i];
    if ( (IT_NONE == x->item) || (0.0f < x->roll) || (RACE_RUNNING != r->phase) ) return 0;
    kart_t *k = &x->k;
    const float fx = cosf(k->angle), fy = sinf(k->angle);
    switch ( x->item )
    {
    case IT_TURBO:
        kart_boost(k, 1.2f, 1.0f);
        break;
    case IT_GOLDEN:
        k->star = ITEM_STAR_TIME;
        kart_boost(k, 1.0f, 0.8f);
        break;
    case IT_PEEL: {
        item_shot_t *s = free_shot(&r->items);
        *s = (item_shot_t){ .kind = IT_PEEL, .owner = (uint8_t)i, .x = k->x - fx * 10.0f, .y = k->y - fy * 10.0f,
                            .life = PEEL_LIFE };
        break;
    }
    case IT_ACORN: {
        item_shot_t *s = free_shot(&r->items);
        *s = (item_shot_t){ .kind = IT_ACORN, .owner = (uint8_t)i, .x = k->x + fx * 9.0f, .y = k->y + fy * 9.0f,
                            .vx = fx * ACORN_SPEED, .vy = fy * ACORN_SPEED, .prog = x->prog,
                            .off = (float)track_offset(r->t, k->x, k->y), .life = ACORN_LIFE };
        break;
    }
    default:
        break;
    }
    if ( 1 < x->stock )
    {
        x->stock--;
    }
    else
    {
        x->item = IT_NONE;
        x->stock = 0;
    }
    x->item_t = 0.0f;
    if ( 0 == i )
    {
        return ITEM_EV_USE;
    }
    return 0;
}

/** @brief An acorn rolls along the track: it steers for a point ahead in its lane. */
static void acorn_step(race_t *r, item_shot_t *s, float dt)
{
    const track_t *t = r->t;
    const uint16_t p = track_prog(t, s->x, s->y);
    if ( TK_NO_PROG != p )
    {
        const int L = t->length;
        int here = s->prog % L;
        if ( 0 > here ) here += L;
        int d = (int)p - here;
        if ( L / 2 < d ) d -= L;
        if ( -L / 2 > d ) d += L;
        if ( (-RACE_MAX_JUMP < d) && (RACE_MAX_JUMP > d) ) s->prog += d;
    }
    const tk_line_t *a = track_at(t, s->prog + 24);
    const float tx = a->x - a->dy * s->off, ty = a->y + a->dx * s->off;
    float dx = tx - s->x, dy = ty - s->y;
    const float l = sqrtf(dx * dx + dy * dy) + 1e-3f;
    s->vx += (dx / l * ACORN_SPEED - s->vx) * fminf(1.0f, 8.0f * dt);
    s->vy += (dy / l * ACORN_SPEED - s->vy) * fminf(1.0f, 8.0f * dt);
    s->x += s->vx * dt;
    s->y += s->vy * dt;
    const tk_surface_t surf = track_surface(t, s->x, s->y);
    if ( (TK_WALL == surf) || (TK_OUT == surf) ) s->kind = IT_NONE;
}

uint32_t race_items_step(race_t *r, float dt)
{
    uint32_t ev = 0;
    items_t *it = &r->items;
    for ( int b = 0; b < it->n_boxes; b++ ) it->boxes[b].t = fmaxf(0.0f, it->boxes[b].t - dt);
    for ( int s = 0; s < it->n_seeds; s++ ) it->seeds[s].t = fmaxf(0.0f, it->seeds[s].t - dt);

    for ( int i = 0; i < r->n; i++ )
    {
        racer_t *x = &r->r[i];
        /* The roulette. */
        if ( 0.0f < x->roll )
        {
            x->roll -= dt;
            if ( 0.0f >= x->roll )
            {
                x->roll = 0.0f;
                if ( 0 == i ) ev |= ITEM_EV_GOT;
            }
        }
        if ( IT_NONE != x->item ) x->item_t += dt;
        /* Boxes: one per kart per pass; empty hands get a roll. */
        for ( int b = 0; b < it->n_boxes; b++ )
        {
            item_spot_t *bx = &it->boxes[b];
            if ( (0.0f < bx->t) || !near(x->k.x, x->k.y, bx->x, bx->y, PICK_R) ) continue;
            bx->t = ITEM_BOX_BACK;
            if ( (IT_NONE == x->item) && (0.0f >= x->roll) )
            {
                x->item = (uint8_t)item_for_place(x->place, r->n, rng_unit(&r->rng));
                x->roll = ITEM_ROLL;
                x->item_t = 0.0f;
                if ( 0 == i ) ev |= ITEM_EV_BOX;
            }
        }
        /* Seeds. */
        for ( int s = 0; s < it->n_seeds; s++ )
        {
            item_spot_t *sd = &it->seeds[s];
            if ( (0.0f < sd->t) || !near(x->k.x, x->k.y, sd->x, sd->y, PICK_R) ) continue;
            sd->t = ITEM_SEED_BACK;
            if ( KART_SEEDS_MAX > x->k.seeds ) x->k.seeds++;
            if ( 0 == i ) ev |= ITEM_EV_SEED;
        }
    }

    /* Shots: move, age, hit. */
    for ( int n = 0; ITEM_MAX_SHOTS > n; n++ )
    {
        item_shot_t *s = &it->shots[n];
        if ( IT_NONE == s->kind ) continue;
        s->age += dt;
        if ( s->life <= s->age )
        {
            s->kind = IT_NONE;
            continue;
        }
        if ( IT_ACORN == s->kind ) acorn_step(r, s, dt);
        if ( IT_NONE == s->kind ) continue;
        for ( int i = 0; i < r->n; i++ )
        {
            kart_t *k = &r->r[i].k;
            if ( (i == s->owner) && (SAFE_AGE > s->age) ) continue;
            if ( (0.0f < k->hop) && (IT_PEEL == s->kind) ) continue;   /* hopped over it */
            if ( !near(k->x, k->y, s->x, s->y, HIT_R) ) continue;
            const bool hurt = (0.0f >= k->star) && (0.0f >= k->spin);
            kart_hit(k);
            if ( (0 == i) && hurt ) ev |= ITEM_EV_HIT;
            if ( (0 == s->owner) && (0 != i) && hurt ) ev |= ITEM_EV_STRUCK;
            s->kind = IT_NONE;
            break;
        }
    }
    return ev;
}
