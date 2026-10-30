/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kart.c
 * @brief kart_step(): the nose turns, the speed follows the throttle and the ground, and the velocity chases the nose at the tyres' grip.
 *
 * The slide is the whole feel: the velocity (vx, vy) is pulled toward the
 * nose's direction at GRIP per second, less while drifting, so a kart that
 * turns hard carries sideways for a moment. Walls are tested per axis, so a
 * kart that clips one slides along it instead of stopping dead.
 */
#include "kart.h"

#include <math.h>

#define GRIP        9.0f    /* how fast the velocity follows the nose, 1/s */
#define DRIFT_GRIP  3.2f
#define ICE_GRIP    1.6f    /* on ice the velocity barely follows the nose */
#define BRAKE       260.0f  /* texels/s^2 */
#define DRAG        1.6f    /* above the top speed: the excess decays at this rate, 1/s */
#define COAST       0.9f    /* off the throttle the speed decays at this rate, 1/s */
#define REVERSE     0.3f    /* of the top speed */
#define STEER_RATE  7.0f    /* how fast the wheel follows the input, 1/s */
#define DRIFT_MIN   55.0f   /* below this speed there is no drift */
#define HOP_TIME    0.22f
#define SPIN_TIME   1.1f
#define PAD_TIME    0.9f

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

/** @brief The share of the top speed a surface allows. */
static float surface_top(tk_surface_t s)
{
    switch ( s )
    {
    case TK_CURB:  return 0.96f;
    case TK_GRASS: return 0.55f;
    case TK_SAND:  return 0.42f;
    default:       return 1.0f;
    }
}

static bool solid(const track_t *t, float x, float y)
{
    const tk_surface_t s = track_surface(t, x, y);
    return (TK_WALL == s) || (TK_OUT == s);
}

void kart_place(kart_t *k, float x, float y, float angle)
{
    *k = (kart_t){ .x = x, .y = y, .angle = angle, .surface = TK_ROAD };
}

void kart_boost(kart_t *k, float seconds, float strength)
{
    /* The longer turbo sets the time; the stronger one the strength. */
    if ( 0.0f >= k->boost )
    {
        k->boost_k = strength;
    }
    else if ( k->boost_k < strength )
    {
        k->boost_k = strength;
    }
    if ( k->boost < seconds )
    {
        k->boost = seconds;
    }
}

void kart_hit(kart_t *k)
{
    if ( (0.0f < k->star) || (0.0f < k->spin) )
    {
        return;
    }
    k->spin = SPIN_TIME;
    k->drift = 0;
    k->charge = 0.0f;
    k->boost = 0.0f;
    if ( 2 < k->seeds )
    {
        k->seeds = (uint8_t)(k->seeds - 2);
    }
    else
    {
        k->seeds = 0;
    }
}

int kart_charge_level(const kart_t *k)
{
    if ( !k->drift )
    {
        return 0;
    }
    if ( KART_DRIFT_ORANGE <= k->charge )
    {
        return 2;
    }
    if ( KART_DRIFT_BLUE <= k->charge )
    {
        return 1;
    }
    return 0;
}

/** @brief The hop button: its press hops, held while steering it drifts, let go it fires. */
static uint32_t drift_step(kart_t *k, const kart_input_t *in, float dt)
{
    uint32_t ev = 0;
    const bool press = in->hop && (!k->hop_held);
    k->hop_held = in->hop;
    if ( press && (0.0f >= k->spin) )
    {
        k->hop = HOP_TIME;
        ev |= KART_EV_HOP;
    }
    if ( k->drift )
    {
        if ( (!in->hop) || (DRIFT_MIN > k->speed) || (0.0f < k->spin) )
        {
            const int level = kart_charge_level(k);
            if ( (false == in->hop) && (0 != level) )
            {
                /* Orange fires a longer, stronger turbo than blue. */
                float seconds = 0.6f;
                float strength = 0.7f;
                if ( 2 == level )
                {
                    seconds = 1.1f;
                    strength = 0.9f;
                }
                kart_boost(k, seconds, strength);
                ev |= KART_EV_MINITURBO;
            }
            k->drift = 0;
            k->charge = 0.0f;
        }
        else
        {
            const int before = kart_charge_level(k);
            /* Steering against the drift charges it more slowly than into it. */
            float rate = 0.6f;
            if ( 0.0f < k->steer * k->drift )
            {
                rate = 1.0f;
            }
            k->charge += dt * rate;
            const int after = kart_charge_level(k);
            if ( before < after )
            {
                if ( 2 == after )
                {
                    ev |= KART_EV_ORANGE;
                }
                else
                {
                    ev |= KART_EV_BLUE;
                }
            }
        }
    }
    else if ( in->hop && (0.3f < fabsf(in->steer)) && (DRIFT_MIN < k->speed) && (0.0f >= k->spin) &&
              (press || (0.0f < k->hop)) )
    {
        /* A drift starts out of a hop, steering one way. */
        if ( 0.0f < in->steer )
        {
            k->drift = 1;
        }
        else
        {
            k->drift = -1;
        }
        k->charge = 0.0f;
        ev |= KART_EV_DRIFT;
    }
    return ev;
}

uint32_t kart_step(kart_t *k, const kart_stats_t *st, const kart_input_t *in, const track_t *t, float dt)
{
    uint32_t ev = 0;
    const bool control = 0.0f >= k->spin;
    /* A spinning kart's wheel drifts back to centre. */
    float steer_in = 0.0f;
    if ( control )
    {
        steer_in = in->steer;
    }
    k->steer += (clampf(steer_in, -1.0f, 1.0f) - k->steer) * clampf(STEER_RATE * dt, 0.0f, 1.0f);
    ev |= drift_step(k, in, dt);

    /* The ground under the kart and what it allows. */
    k->surface = (uint8_t)track_surface(t, k->x, k->y);
    if ( (TK_BOOST == k->surface) && (0.0f >= k->hop) )
    {
        if ( PAD_TIME - 0.1f > k->boost )
        {
            ev |= KART_EV_PAD;
        }
        kart_boost(k, PAD_TIME, 1.0f);
    }
    float top = st->top * (1.0f + 0.02f * (float)k->seeds);
    /* Airborne, the ground does not slow you; a star and a turbo ignore it too. */
    if ( (0.0f >= k->hop) && (0.0f >= k->boost) && (0.0f >= k->star) )
    {
        top *= surface_top((tk_surface_t)k->surface);
    }
    if ( 0.0f < k->boost )
    {
        top = st->top * (1.0f + 0.45f * k->boost_k);
    }
    if ( 0.0f < k->star )
    {
        top = fmaxf(top, st->top * 1.25f);
    }

    /* Throttle, brake, drag. */
    if ( !control )
    {
        k->speed -= k->speed * 2.5f * dt;
    }
    else if ( in->brake )
    {
        k->speed -= BRAKE * dt;
        if ( -st->top * REVERSE > k->speed )
        {
            k->speed = -st->top * REVERSE;
        }
    }
    else if ( (!in->gas) && (0.0f >= k->boost) )
    {
        /* Coasting: rolling resistance, and the rough slows harder. */
        k->speed -= k->speed * COAST * dt;
        if ( top < k->speed )
        {
            k->speed -= (k->speed - top) * DRAG * dt;
        }
        if ( 1.0f > fabsf(k->speed) )
        {
            k->speed = 0.0f;
        }
    }
    else if ( top > k->speed )
    {
        /* A turbo triples the acceleration. */
        float a = st->accel;
        if ( 0.0f < k->boost )
        {
            a = st->accel * 3.0f;
        }
        /* Strong from rest, gentler near the top. */
        k->speed += a * dt * (1.0f - 0.6f * clampf(k->speed / top, 0.0f, 1.0f));
        if ( top < k->speed )
        {
            k->speed = top;
        }
    }
    else
    {
        k->speed -= (k->speed - top) * DRAG * dt;
    }

    /* Turning: little at a crawl, full from a third of the top speed. */
    const float grip_speed = clampf(fabsf(k->speed) / (st->top * 0.33f), 0.0f, 1.0f);
    float turn;
    if ( k->drift )
    {
        /* Into the drift: tight; out of it: wide; never straight. */
        const float into = k->steer * (float)k->drift;
        turn = (float)k->drift * st->turn * (0.75f + 0.45f * into);
    }
    else
    {
        turn = k->steer * st->turn;
    }
    /* Reversing turns the nose the other way. */
    float dir = 1.0f;
    if ( 0.0f > k->speed )
    {
        dir = -1.0f;
    }
    k->angle += turn * grip_speed * dt * dir;
    if ( 0.0f < k->spin )
    {
        k->angle += 12.0f * dt;
    }
    if ( (float)M_PI < k->angle )
    {
        k->angle -= 2.0f * (float)M_PI;
    }
    if ( -(float)M_PI > k->angle )
    {
        k->angle += 2.0f * (float)M_PI;
    }

    /* The velocity chases the nose. */
    const float nx = cosf(k->angle) * k->speed, ny = sinf(k->angle) * k->speed;
    const bool ice = (TK_ICE == k->surface) && (0.0f >= k->hop) && (0.0f >= k->star);
    float grip = GRIP;
    if ( ice )
    {
        grip = ICE_GRIP;
    }
    else if ( k->drift )
    {
        grip = DRIFT_GRIP;
    }
    const float g = clampf(grip * dt, 0.0f, 1.0f);
    k->vx += (nx - k->vx) * g;
    k->vy += (ny - k->vy) * g;

    /* Move, one axis at a time against the walls. */
    const float px = k->x + k->vx * dt, py = k->y + k->vy * dt;
    /* Test the side of the kart that leads on each axis. */
    float rx = -KART_RADIUS;
    float ry = -KART_RADIUS;
    if ( 0 <= k->vx )
    {
        rx = KART_RADIUS;
    }
    if ( 0 <= k->vy )
    {
        ry = KART_RADIUS;
    }
    bool hit = false;
    if ( !solid(t, px + rx, k->y) )
    {
        k->x = px;
    }
    else
    {
        k->vx *= -0.3f;
        hit = true;
    }
    if ( !solid(t, k->x, py + ry) )
    {
        k->y = py;
    }
    else
    {
        k->vy *= -0.3f;
        hit = true;
    }
    if ( hit )
    {
        ev |= KART_EV_WALL;
        k->speed *= 0.55f;
        k->drift = 0;
        k->charge = 0.0f;
    }

    k->boost = fmaxf(0.0f, k->boost - dt);
    k->hop = fmaxf(0.0f, k->hop - dt);
    k->spin = fmaxf(0.0f, k->spin - dt);
    k->star = fmaxf(0.0f, k->star - dt);
    return ev;
}
