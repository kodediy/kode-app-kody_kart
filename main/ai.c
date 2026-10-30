/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ai.c
 * @brief ai_drive(): chase a point ahead on the racing line.
 *
 * The racing line is the centre line pulled toward the inside of the bend
 * ahead, plus the rival's own lane (which it changes every few seconds, so
 * the pack spreads). The point it steers at lies further ahead the faster it
 * goes. It brakes when the angle to that point is large at speed, and backs
 * off a wall it is stuck against. The rubber band: a rival far ahead of the
 * player eases off, one far behind finds a little more.
 */
#include "ai.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static float clampf(float v, float lo, float hi)
{
    if ( lo > v )
    {
        return lo;
    }
    if ( hi < v )
    {
        return hi;
    }
    return v;
}

static float wrap_pi(float a)
{
    while ( (float)M_PI < a )
    {
        a -= 2.0f * (float)M_PI;
    }
    while ( -(float)M_PI > a )
    {
        a += 2.0f * (float)M_PI;
    }
    return a;
}

static uint32_t rng_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

/** @brief Whether the rival should use what it holds now. */
static bool want_item(const race_t *r, int i)
{
    const racer_t *x = &r->r[i];
    if ( (IT_NONE == x->item) || (0.0f < x->roll) || (0.4f > x->item_t) )
    {
        return false;
    }
    const track_t *t = r->t;
    switch ( x->item )
    {
    case IT_GOLDEN:
        return true;
    case IT_TURBO: {
        /* On a straight and on the road: the turbo is not wasted in a wall. */
        float bend = 0.0f;
        for ( int k = 0; 80 > k; k += 10 )
        {
            bend += fabsf(track_at(t, x->prog + k)->curv);
        }
        return (0.04f > bend) && (TK_ROAD == x->k.surface);
    }
    case IT_PEEL:
        for ( int j = 0; j < r->n; j++ )
        {
            const int gap = x->prog - r->r[j].prog;   /* > 0: j is behind */
            if ( (j != i) && (8 < gap) && (45 > gap) )
            {
                return true;
            }
        }
        return 6.0f < x->item_t;
    case IT_ACORN: {
        const int lane = track_offset(t, x->k.x, x->k.y);
        for ( int j = 0; j < r->n; j++ )
        {
            const int gap = r->r[j].prog - x->prog;   /* > 0: j is ahead */
            if ( (j == i) || (25 > gap) || (180 < gap) )
            {
                continue;
            }
            if ( 12 > abs(track_offset(t, r->r[j].k.x, r->r[j].k.y) - lane) )
            {
                return true;
            }
        }
        return 8.0f < x->item_t;
    }
    default:
        return false;
    }
}

void ai_drive(race_t *r, int i, float dt, kart_input_t *in, kart_stats_t *st)
{
    racer_t *x = &r->r[i];
    const track_t *t = r->t;
    const float hw = (float)t->def->half_w;
    *in = (kart_input_t){ .gas = true };

    /* A new lane now and then. */
    x->lane_t -= dt;
    if ( 0.0f >= x->lane_t )
    {
        x->lane = ((float)(rng_next(&x->rng) % 1000) / 1000.0f - 0.5f) * hw * 0.8f;
        x->lane_t = 2.0f + (float)(rng_next(&x->rng) % 3000) / 1000.0f;
    }

    /* The point to steer at, pulled to the inside of the bend around it. */
    const float speed = fabsf(x->k.speed);
    const int ahead = (int)(16.0f + speed * 0.22f);
    const int s = x->prog + ahead;
    const tk_line_t *p = track_at(t, s);
    float bend = 0.0f;
    for ( int k = -8; 8 >= k; k += 4 )
    {
        bend += track_at(t, s + k)->curv;
    }
    bend /= 5.0f;
    float side = -1.0f;
    if ( 0.0f < bend )
    {
        side = 1.0f;
    }
    const float inside = side * hw * 0.5f * clampf(fabsf(bend) / 0.012f, 0.0f, 1.0f);
    const float off = clampf(inside + x->lane, -hw * 0.75f, hw * 0.75f);
    const float tx = p->x - p->dy * off, ty = p->y + p->dx * off;

    const float want = atan2f(ty - x->k.y, tx - x->k.x);
    const float diff = wrap_pi(want - x->k.angle);
    in->steer = clampf(diff * 3.0f, -1.0f, 1.0f);

    /* Brake for a sharp bend further on, harder for a clumsy driver. */
    const tk_line_t *far = track_at(t, x->prog + ahead * 2);
    const float sharp = fabsf(far->curv) + fabsf(p->curv);
    if ( ((0.7f < fabsf(diff)) && (st->top * 0.55f < speed)) || ((0.045f < sharp) && (st->top * 0.8f < speed)) )
    {
        in->brake = true;
    }

    /* Stuck against something: back off it for a moment. */
    if ( (6.0f > speed) && (RACE_RUNNING == r->phase) )
    {
        x->stuck += dt;
    }
    else if ( 1.2f > x->stuck )
    {
        x->stuck = 0.0f;
    }
    if ( 1.2f <= x->stuck )
    {
        in->brake = true;
        in->steer = -in->steer;
        x->stuck += dt;
        if ( 2.0f < x->stuck )
        {
            x->stuck = 0.0f;
        }
    }

    in->use = (0 != i) && want_item(r, i);

    /* A long bend at speed: hop into a drift toward its inside, hold it while
     * the bend lasts, let go on the way out for the mini-turbo. */
    float coming = 0.0f;
    for ( int k = 0; 60 > k; k += 10 )
    {
        coming += track_at(t, x->prog + k)->curv;
    }
    coming /= 6.0f;
    const bool bendy = (0.011f < fabsf(coming)) && (st->top * 0.6f < speed) && (TK_ROAD == x->k.surface);
    if ( x->k.drift )
    {
        in->hop = bendy && ((0.0f < coming) == (0 < x->k.drift));
        if ( in->hop )
        {
            in->steer = clampf(in->steer + 0.3f * (float)x->k.drift, -1.0f, 1.0f);
        }
    }
    else if ( bendy && (!x->k.hop_held) && (0.98f < x->skill) )
    {
        in->hop = true;   /* only the better drivers bother */
        if ( 0.0f < coming )
        {
            in->steer = fmaxf(in->steer, 0.5f);
        }
        else
        {
            in->steer = fminf(in->steer, -0.5f);
        }
    }

    /* Skill, and the rubber band to the player (racer 0). */
    float k = x->skill;
    if ( 0 != i )
    {
        const int gap = r->r[0].prog - x->prog;   /* > 0: the player is ahead */
        /* Gentle (Pablo asked for a fair race): at most 5 % either way. */
        if ( 150 < gap )
        {
            k *= 1.0f + clampf((float)(gap - 150) / 4000.0f, 0.0f, 0.05f);
        }
        if ( -150 > gap )
        {
            k *= 1.0f - clampf((float)(-gap - 150) / 4000.0f, 0.0f, 0.05f);
        }
    }
    st->top *= k;
    st->accel *= k;
}
