/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file race.c
 * @brief race_step(): the countdown, then every kart, its progress, laps, places.
 */
#include "race.h"

#include <math.h>
#include <string.h>

#include "ai.h"

#define GRID_FIRST  10      /* texels behind the line: the first slot */
#define GRID_GAP    16      /* texels between slots */
#define WRONG_AFTER 1.0f    /* seconds against the circuit before the sign */

kart_stats_t race_stats(race_cc_t cc)
{
    static const kart_stats_t CC[] = {
        { .top = 115.0f, .accel = 95.0f, .turn = 2.3f },
        { .top = 140.0f, .accel = 110.0f, .turn = 2.3f },
        { .top = 165.0f, .accel = 125.0f, .turn = 2.4f },
    };
    race_cc_t pick = RACE_100CC;
    if ( RACE_150CC >= cc )
    {
        pick = cc;
    }
    return CC[pick];
}

static uint32_t rng_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

void race_init(race_t *r, const track_t *t, race_cc_t cc, uint32_t seed)
{
    memset(r, 0, sizeof *r);
    r->t = t;
    r->laps = t->def->laps;
    r->cc = (uint8_t)cc;
    r->clock = -RACE_COUNTDOWN;
    if ( 0u != seed )
    {
        r->rng = seed;
    }
    else
    {
        r->rng = 0x9E3779B9u;
    }
    r->n = RACE_KARTS;
    for ( int i = 0; RACE_KARTS > i; i++ )
    {
        racer_t *x = &r->r[i];
        /* The player starts at the back, rivals in front: there is someone to chase. */
        int slot = i - 1;
        if ( 0 == i )
        {
            slot = RACE_KARTS - 1;
        }
        const int behind = GRID_FIRST + GRID_GAP * slot;
        const tk_line_t *g = track_at(t, -behind);
        float side = -t->def->half_w / 2.0f;
        if ( 0 != (slot & 1) )
        {
            side = t->def->half_w / 2.0f;
        }
        kart_place(&x->k, g->x - g->dy * side, g->y + g->dx * side, atan2f(g->dy, g->dx));
        x->who = (uint8_t)i;
        x->cpu = 0 != i;
        x->prog = -behind;
        x->place = (uint8_t)(slot + 1);
        x->best_lap = 0.0f;
        x->rng = rng_next(&r->rng) | 1u;
        /* The same engine as the player on average: a rival is 96 to 104 % of it. */
        x->skill = 0.96f + 0.08f * (float)(rng_next(&r->rng) % 1000) / 1000.0f;
    }
    race_items_init(r);
}

void race_time_trial(race_t *r)
{
    /* Alone on the line, front slot, no boxes, no seeds: three turbos in hand. */
    r->n = 1;
    racer_t *x = &r->r[0];
    const tk_line_t *g = track_at(r->t, -GRID_FIRST);
    kart_place(&x->k, g->x, g->y, atan2f(g->dy, g->dx));
    x->prog = -GRID_FIRST;
    x->place = 1;
    x->item = IT_TURBO;
    x->stock = 3;
    r->items.n_boxes = 0;
    r->items.n_seeds = 0;
}

int race_count(const race_t *r)
{
    if ( RACE_COUNTING != r->phase ) return 0;
    return (int)ceilf(-r->clock - 1e-3f);   /* steps of 1/60 s sum a hair short */
}

bool race_wrong_way(const race_t *r) { return WRONG_AFTER <= r->r[0].wrong; }

int race_lap_of(const race_t *r, int racer) { return r->r[racer].lap; }

/** @brief Read the progress map under the kart and move prog by the change, if it is a drive. */
static void track_progress(race_t *r, racer_t *x)
{
    const int L = r->t->length;
    const uint16_t s = track_prog(r->t, x->k.x, x->k.y);
    if ( TK_NO_PROG == s ) return;
    int here = x->prog % L;
    if ( 0 > here ) here += L;
    int d = (int)s - here;
    if ( L / 2 < d ) d -= L;
    if ( -L / 2 > d ) d += L;
    if ( (-RACE_MAX_JUMP < d) && (RACE_MAX_JUMP > d) ) x->prog += d;

    /* Against the circuit: the nose more than ~110 degrees off its direction, moving. */
    const tk_line_t *l = track_at(r->t, s);
    const float dot = cosf(x->k.angle) * l->dx + sinf(x->k.angle) * l->dy;
    if ( (-0.35f > dot) && (20.0f < fabsf(x->k.speed)) )
    {
        x->wrong = x->wrong + 1.0f / 60.0f;
    }
    else
    {
        x->wrong = 0.0f;
    }
}

/** @brief Move a kart aside unless that would put it in a wall (it could never drive out). */
static void nudge(const track_t *t, kart_t *k, float dx, float dy)
{
    const tk_surface_t s = track_surface(t, k->x + dx, k->y + dy);
    if ( (TK_WALL == s) || (TK_OUT == s) ) return;
    k->x += dx;
    k->y += dy;
}

/**
 * @brief Karts are discs of KART_RADIUS: overlapping ones are pushed apart
 *        and trade the part of their velocity along the line between them.
 *        A kart with a star knocks the other into a spin.
 * @return whether the player touched anyone.
 */
static bool collide(race_t *r)
{
    bool player = false;
    const float min_d = 2.0f * KART_RADIUS;
    for ( int i = 0; i < r->n; i++ )
        for ( int j = i + 1; j < r->n; j++ )
        {
            kart_t *a = &r->r[i].k, *b = &r->r[j].k;
            const float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
            if ( min_d * min_d <= d2 ) continue;
            const float d = sqrtf(d2) + 1e-3f, nx = dx / d, ny = dy / d;
            const float push = 0.5f * (min_d - d);
            nudge(r->t, a, -nx * push, -ny * push);
            nudge(r->t, b, nx * push, ny * push);
            /* Approaching along the normal: swap most of it (a bouncy, equal-mass bump). */
            const float va = a->vx * nx + a->vy * ny, vb = b->vx * nx + b->vy * ny;
            if ( va - vb > 0.0f )
            {
                const float dv = (va - vb) * 0.8f;
                a->vx -= nx * dv;
                a->vy -= ny * dv;
                b->vx += nx * dv;
                b->vy += ny * dv;
            }
            if ( (0.0f < a->star) && (0.0f >= b->star) ) kart_hit(b);
            if ( (0.0f < b->star) && (0.0f >= a->star) ) kart_hit(a);
            if ( 0 == i ) player = true;
        }
    return player;
}

/** @brief Places: finishers in their order, then the rest by progress. */
static void rank(race_t *r)
{
    for ( int i = 0; i < r->n; i++ )
    {
        racer_t *a = &r->r[i];
        int place = 1;
        for ( int j = 0; j < r->n; j++ )
        {
            if ( j == i ) continue;
            const racer_t *b = &r->r[j];
            bool ahead;
            if ( a->finished || b->finished )
            {
                ahead = b->finished && (!a->finished || b->finish_time < a->finish_time ||
                                        (b->finish_time == a->finish_time && j < i));
            }
            else
            {
                ahead = b->prog > a->prog || (b->prog == a->prog && j < i);
            }
            if ( ahead ) place++;
        }
        if ( a->place != place ) a->place_at = r->clock;
        a->place = (uint8_t)place;
    }
}

uint32_t race_step(race_t *r, const kart_input_t *in, float dt, uint32_t *kart_ev, uint32_t *item_ev)
{
    uint32_t ev = 0;
    *kart_ev = 0;
    *item_ev = 0;
    const int count_before = race_count(r);
    r->clock += dt;
    if ( RACE_COUNTING == r->phase )
    {
        if ( race_count(r) != count_before ) ev |= RACE_EV_COUNT;
        /* The player's gas through the countdown, for the start. */
        racer_t *p = &r->r[0];
        if ( !p->cpu && in->gas )
        {
            if ( 0.0f == p->gas_from ) p->gas_from = r->clock;
        }
        else
        {
            p->gas_from = 0.0f;
        }
        if ( 0.0f <= r->clock )
        {
            r->phase = RACE_RUNNING;
            r->clock = 0.0f;
            ev |= RACE_EV_GO;
            for ( int i = 0; i < r->n; i++ ) r->r[i].lap_t0 = 0.0f;
            if ( 0.0f != p->gas_from )
            {
                const float before = -p->gas_from;   /* seconds before GO */
                if ( (ROCKET_FROM >= before) && (ROCKET_TO <= before) )
                {
                    kart_boost(&p->k, 1.0f, 1.0f);
                    p->k.speed = race_stats((race_cc_t)r->cc).top * 0.6f;
                    ev |= RACE_EV_ROCKET;
                }
                else if ( ROCKET_EARLY < before )
                {
                    p->k.spin = 0.7f;
                    ev |= RACE_EV_STALL;
                }
            }
        }
        else
        {
            return ev;   /* engines idle on the grid */
        }
    }

    const kart_stats_t base = race_stats((race_cc_t)r->cc);
    const uint8_t place_before = r->r[0].place;
    for ( int i = 0; i < r->n; i++ )
    {
        racer_t *x = &r->r[i];
        kart_input_t ci = { 0 };
        kart_stats_t st = base;
        const kart_input_t *use = in;
        if ( x->cpu || x->finished )
        {
            /* A finished player is driven home by the same brain. */
            ai_drive(r, i, dt, &ci, &st);
            use = &ci;
        }
        if ( use->use ) *item_ev |= race_use_item(r, i);
        const uint32_t kev = kart_step(&x->k, &st, use, r->t, dt);
        if ( 0 == i ) *kart_ev |= kev;
        track_progress(r, x);

        int lap_now = 0;
        if ( 0 <= x->prog )
        {
            lap_now = x->prog / r->t->length + 1;
        }
        if ( (x->lap < lap_now) && !x->finished )
        {
            if ( 1 <= x->lap )
            {
                x->last_lap = r->clock - x->lap_t0;
                if ( (0.0f >= x->best_lap) || (x->best_lap > x->last_lap) )
                {
                    x->best_lap = x->last_lap;
                    if ( (0 == i) && (1 <= x->lap) ) ev |= RACE_EV_BEST_LAP;
                }
            }
            x->lap = (int16_t)lap_now;
            x->lap_t0 = r->clock;
            if ( x->lap > r->laps )
            {
                x->finished = true;
                x->finish_time = r->clock;
                r->finishers++;
                if ( 0 == i ) ev |= RACE_EV_FINISH;
            }
            else if ( (0 == i) && (1 < x->lap) )
            {
                if ( r->laps == x->lap )
                {
                    ev |= RACE_EV_FINAL_LAP | RACE_EV_LAP;
                }
                else
                {
                    ev |= RACE_EV_LAP;
                }
            }
        }
    }
    *item_ev |= race_items_step(r, dt);
    if ( collide(r) ) ev |= RACE_EV_BUMP;
    rank(r);
    if ( r->r[0].place < place_before ) ev |= RACE_EV_OVERTAKE;
    if ( r->r[0].place > place_before ) ev |= RACE_EV_OVERTAKEN;
    if ( r->r[0].finished && (RACE_RUNNING == r->phase) ) r->phase = RACE_DONE;
    return ev;
}
