/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kart_ao.c
 * @brief The race on the glass, from an Active Object of this app.
 *
 * No LVGL: a race redraws the whole glass every frame, and the canvas path
 * (draw buffers a tenth of the glass at a time) held it at 17 fps. The game
 * paints a logical frame half the panel's size (FB_W x FB_H); this doubles
 * every pixel straight into the panel's back buffer and flips it
 * (kdrv_display_back_buffer: no copy). The AO subscribes to the panel and the
 * touch itself, which is all kscreen_start() did.
 *
 * The loop: a static FRAME event posted to itself. kdrv_display_flush_wait()
 * on the previous flip is the pacing (the panel flips at its own frame
 * boundary); one tick of sleep per frame lets the idle task have its turn.
 */
#include "kart_ao.h"

#include <math.h>
#include <string.h>

#include "kdrv_buttons.h"
#include "kdrv_display.h"

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "font.h"
#include "game.h"
#include "ghost.h"
#include "console.h"
#include "hud.h"
#include "kart.h"
#include "race.h"
#include "records.h"
#include "palette.h"
#include "render.h"
#include "screens.h"
#include "sound.h"
#include "track.h"

#define TAG "kart_ao"

#define SC_W 410
#define SC_H 502
#define FB_W RD_W
#define FB_H RD_H
_Static_assert((SC_W == FB_W * 2) && (SC_H == FB_H * 2), "the logical frame is half the glass");

#define FLUSH_WAIT_MS 200
#define FPS_EVERY_MS  5000u

enum KartSignals {
    KART_FRAME_SIG = MAX_SIG,  /**< one frame: step, paint, flip; posted to itself */
    KART_START_SIG,            /**< the console asks for a race: the order waits in s_start */
};

static const QEvt FRAME_EVT = QEVT_INITIALIZER(KART_FRAME_SIG);
static const QEvt START_EVT = QEVT_INITIALIZER(KART_START_SIG);

/** @brief The console's one-slot mailbox: written there, read here, then freed. */
static volatile struct { uint8_t busy, mode, cc; } s_start;

typedef struct {
    QActive    super;
    uint16_t  *fb;             /**< FB_W x FB_H, what the game paints */
    bool       in_flight;      /**< a flip was posted and not waited for yet */
    uint32_t   fps_t0;
    uint16_t   fps_n;
    uint32_t   busy_us;        /**< paint time over the last second */
    uint32_t   up_us;          /**< doubling into the back buffer, over the last second */
    track_t    track;
    race_t     race;           /**< racer 0 is Kody (a rival too, in the demo behind the title) */
    game_t     game;           /**< which screen, the menu's choices */
    uint32_t   done_at;        /**< when the race ended for the player; 0 = not yet */
    uint8_t    follow;         /**< the racer the camera follows */
    records_t  records;        /**< saved with the SDK */
    ghost_t   *ghost_rec;      /**< the time trial being driven, recorded (PSRAM) */
    ghost_t   *ghost_best;     /**< the circuit's record, played back (PSRAM) */
    uint8_t    fps;            /**< last second's frames */
    uint32_t   news;           /**< REC_NEW_* from the last race */
    uint32_t   led_rgb;        /**< what the LED shows now; 0xFFFFFFFF = not set yet */
    uint32_t   rumble_at;      /**< next off-road buzz */
    uint32_t   bot_down_ms;    /**< when BOT went down; 0 = it is up */
    bool       use_item;       /**< BOT was tapped: use the item next step */
    bool       touching;       /**< a finger is down: one touch, one press */
    uint32_t   race_ev, kart_ev, item_ev;   /**< what happened since the last frame */
    float      cam_angle;      /**< the chase camera's heading: lags the kart's */
    float      cam_kick;       /**< 0..1: how much a turbo widens the view */
    float      shake;          /**< seconds of camera shake left */
    uint32_t   last_ms;
    float      acc;            /**< seconds not yet stepped */
} KartAo;

static QEvtPtr     l_queue[KQP_AO_QUEUE_LEN];
static StackType_t l_stack[KQP_AO_STACK_BYTES / sizeof(StackType_t)];

static KartAo l_ao;
QActive * const AO_Kart = &l_ao.super;

static QState KartAo_initial(KartAo * const me, void const * const par);
static QState KartAo_running(KartAo * const me, QEvt const * const e);
static QState KartAo_waiting(KartAo * const me, QEvt const * const e);
static QState KartAo_racing(KartAo * const me, QEvt const * const e);

/* ---- the picture ------------------------------------------------------------ */

/** @brief Double every logical pixel into the panel: one word per pixel pair. */
static void upscale(const uint16_t *src, uint16_t *dst)
{
    for ( int y = 0; FB_H > y; y++ )
    {
        const uint16_t *s = src + y * FB_W;
        uint32_t *d0 = (uint32_t *)(dst + (2 * y) * SC_W);
        for ( int x = 0; FB_W > x; x++ )
        {
            d0[x] = (uint32_t)s[x] * 0x00010001u;
        }
        memcpy(dst + (2 * y + 1) * SC_W, d0, SC_W * sizeof(uint16_t));
    }
}

#define STEP      (1.0f / 60.0f)
#define CAM_BACK  22.0f
#define CAM_LAG   6.0f    /* how fast the camera swings round behind the kart, 1/s */

#define HOME_HOLD_MS 1000   /* BOT held this long goes home; shorter uses the item */

/**
 * @brief The buttons, as the kart wants them: TOP is the throttle (the
 *        Dot's "A"), the pad steers, DOWN brakes, UP hops and drifts. BOT
 *        tapped uses the item - that arrives as an edge from the events.
 */
static kart_input_t read_input(KartAo * const me)
{
    const uint32_t b = kode_buttons_read();
    kart_input_t in = { 0 };
    if ( b & (1u << KDRV_BTN_LEFT) ) in.steer -= 1.0f;
    if ( b & (1u << KDRV_BTN_RIGHT) ) in.steer += 1.0f;
    in.gas = 0 != (b & (1u << KDRV_BTN_TOP));
    in.brake = 0 != (b & (1u << KDRV_BTN_DOWN));
    in.hop = 0 != (b & (1u << KDRV_BTN_UP));
    in.use = me->use_item;
    me->use_item = false;
    return in;
}

static float wrap_pi(float a)
{
    while ( (float)M_PI < a ) a -= 2.0f * (float)M_PI;
    while ( -(float)M_PI > a ) a += 2.0f * (float)M_PI;
    return a;
}

/** @brief Fixed steps of physics for the time that passed. */
static void step(KartAo * const me, uint32_t now)
{
    me->acc += (float)(now - me->last_ms) / 1000.0f;
    me->last_ms = now;
    if ( 4 * STEP < me->acc ) me->acc = 4 * STEP;   /* a long stall is not a teleport */
    kart_input_t in = { 0 };
    if ( GM_RACE == me->game.screen ) in = read_input(me);
    if ( GM_PAUSE == me->game.screen ) me->acc = 0.0f;
    while ( STEP <= me->acc )
    {
        me->acc -= STEP;
        uint32_t kev, iev;
        me->race_ev |= race_step(&me->race, &in, STEP, &kev, &iev);
        me->kart_ev |= kev;
        me->item_ev |= iev;
        if ( me->ghost_rec && (1 == me->race.n) && (RACE_RUNNING == me->race.phase) && !me->race.r[0].finished )
        {
            ghost_add(me->ghost_rec, me->race.clock, me->race.r[0].k.x, me->race.r[0].k.y, me->race.r[0].k.angle);
        }
        in.use = false;   /* one step only */
        const kart_t * const k = &me->race.r[me->follow].k;
        /* The camera follows where the kart goes rather than where it points: a drift swings the view. */
        float travel = k->angle;
        if ( 20.0f < hypotf(k->vx, k->vy) )
        {
            travel = atan2f(k->vy, k->vx);
        }
        float target = travel;
        if ( 0.0f > k->speed )
        {
            target = k->angle;
        }
        me->cam_angle = wrap_pi(me->cam_angle + wrap_pi(target - me->cam_angle) * fminf(1.0f, CAM_LAG * STEP));
        /* A turbo pulls the view wide and back; it eases out when the turbo ends. */
        float kick = 0.0f;
        if ( (0.0f < k->boost) || (0.0f < k->star) )
        {
            kick = 1.0f;
        }
        float ease = 2.5f;   /* per second: in fast, out slow */
        if ( me->cam_kick < kick )
        {
            ease = 8.0f;
        }
        me->cam_kick += (kick - me->cam_kick) * fminf(1.0f, ease * STEP);
        me->shake = fmaxf(0.0f, me->shake - STEP);
    }
}

static void chase_camera(const KartAo * const me, rd_camera_t *cam, uint32_t now)
{
    cam->angle = me->cam_angle;
    const kart_t * const k = &me->race.r[me->follow].k;
    const float back = CAM_BACK + 5.0f * me->cam_kick;
    cam->x = k->x - cosf(cam->angle) * back;
    cam->y = k->y - sinf(cam->angle) * back;
    cam->height = 17.0f;
    cam->focal = 110.0f - 22.0f * me->cam_kick;
    /* The fly-in: through the countdown's first moments the eye swoops down from
     * high over the grid, swinging round, to its place behind the kart. */
    const race_t * const r = &me->race;
    if ( (RACE_COUNTING == r->phase) && !r->r[0].cpu )
    {
        const float u = fminf(1.0f, (r->clock + RACE_COUNTDOWN) / 1.6f);   /* 0 .. 1 */
        const float e = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);       /* ease out */
        const float far = 1.0f - e;
        const float swing = 1.2f * far;
        cam->angle = me->cam_angle + swing;
        const float dist = back + 90.0f * far;
        cam->x = k->x - cosf(cam->angle) * dist;
        cam->y = k->y - sinf(cam->angle) * dist;
        cam->height = 17.0f + 60.0f * far;
    }
    if ( 0.0f < me->shake )
    {
        /* A jolt: a quick wobble of the eye's height and heading, dying out. */
        const float a = me->shake * 4.0f;
        cam->height += a * sinf((float)now * 0.09f);
        cam->angle += 0.02f * a * sinf((float)now * 0.13f);
    }
}

/* ---- what the player hears --------------------------------------------------- */

static const uint16_t SND_COUNT[]     = { 523, 140 };
static const uint16_t SND_GO[]        = { 1047, 320 };
static const uint16_t SND_LAP[]       = { 784, 90, 1047, 140 };
static const uint16_t SND_FINAL[]     = { 784, 100, 784, 100, 784, 100, 1047, 260, 0, 40, 988, 120, 1047, 300 };
static const uint16_t SND_BLUE[]      = { 1319, 45 };
static const uint16_t SND_ORANGE[]    = { 1760, 45 };
static const uint16_t SND_TURBO[]     = { 880, 50, 1175, 50, 1568, 90 };
static const uint16_t SND_PAD[]       = { 1175, 60, 1568, 80 };
static const uint16_t SND_WALL[]      = { 160, 70 };
static const uint16_t SND_BUMP[]      = { 260, 40 };
static const uint16_t SND_PASS[]      = { 1568, 40, 2093, 60 };
static const uint16_t SND_HOP[]       = { 660, 30 };
static const uint16_t SND_BOX[]       = { 1568, 30, 1760, 30, 1976, 30, 2093, 30, 1976, 30, 2093, 30 };
static const uint16_t SND_GOT[]       = { 2093, 70, 2637, 110 };
static const uint16_t SND_USE[]       = { 988, 40, 659, 60 };
static const uint16_t SND_HIT[]       = { 784, 90, 622, 90, 523, 90, 392, 220 };
static const uint16_t SND_SEED[]      = { 1976, 35, 2637, 70 };
static const uint16_t SND_STRUCK[]    = { 1319, 60, 1760, 60, 2093, 120 };
static const uint16_t SND_ROCKET[]    = { 1047, 200, 1568, 50, 2093, 160 };
static const uint16_t SND_STALL[]     = { 196, 80, 0, 40, 175, 80, 0, 40, 147, 160 };
static const uint16_t SND_WIN[]       = { 1047, 120, 1319, 120, 1568, 120, 2093, 360, 0, 60, 1568, 120, 2093, 480 };
static const uint16_t SND_PODIUM[]    = { 784, 140, 988, 140, 1175, 140, 1568, 400 };
static const uint16_t SND_FINISHED[]  = { 659, 160, 587, 160, 523, 360 };

#define PLAY(tune, prio) sound_play((tune), (int)(sizeof(tune) / sizeof((tune)[0]) / 2), (prio))

static void sounds(KartAo * const me)
{
    const uint32_t re = me->race_ev, ke = me->kart_ev, ie = me->item_ev;
    if ( ie & ITEM_EV_HIT )
    {
        PLAY(SND_HIT, SND_HIGH);
        return;
    }
    if ( re & RACE_EV_FINISH )
    {
        const int place = me->race.r[0].place;
        if ( 1 == place ) PLAY(SND_WIN, SND_HIGH);
        else if ( 3 >= place ) PLAY(SND_PODIUM, SND_HIGH);
        else PLAY(SND_FINISHED, SND_HIGH);
    }
    else if ( re & RACE_EV_FINAL_LAP ) PLAY(SND_FINAL, SND_HIGH);
    else if ( re & RACE_EV_ROCKET ) PLAY(SND_ROCKET, SND_HIGH);
    else if ( re & RACE_EV_STALL ) PLAY(SND_STALL, SND_HIGH);
    else if ( re & RACE_EV_GO ) PLAY(SND_GO, SND_HIGH);
    else if ( re & RACE_EV_COUNT ) PLAY(SND_COUNT, SND_HIGH);
    else if ( re & RACE_EV_LAP ) PLAY(SND_LAP, SND_MID);
    else if ( ie & ITEM_EV_STRUCK ) PLAY(SND_STRUCK, SND_MID);
    else if ( ie & ITEM_EV_GOT ) PLAY(SND_GOT, SND_MID);
    else if ( ie & ITEM_EV_BOX ) PLAY(SND_BOX, SND_MID);
    else if ( ie & ITEM_EV_USE ) PLAY(SND_USE, SND_MID);
    else if ( ke & KART_EV_MINITURBO ) PLAY(SND_TURBO, SND_MID);
    else if ( ie & ITEM_EV_SEED ) PLAY(SND_SEED, SND_LOW);
    else if ( ke & KART_EV_PAD ) PLAY(SND_PAD, SND_MID);
    else if ( ke & KART_EV_ORANGE ) PLAY(SND_ORANGE, SND_LOW);
    else if ( ke & KART_EV_BLUE ) PLAY(SND_BLUE, SND_LOW);
    else if ( ke & KART_EV_WALL ) PLAY(SND_WALL, SND_LOW);
    else if ( re & RACE_EV_BUMP ) PLAY(SND_BUMP, SND_LOW);
    else if ( re & RACE_EV_OVERTAKE ) PLAY(SND_PASS, SND_LOW);
    else if ( ke & KART_EV_HOP ) PLAY(SND_HOP, SND_LOW);
}

/* ---- what the player feels and sees on the LED ---------------------------- */

static void buzz(uint16_t ms, uint8_t gain) { (void)kode_haptic_play(ms, gain); }

static void feel(KartAo * const me, uint32_t now)
{
    const uint32_t re = me->race_ev, ke = me->kart_ev, ie = me->item_ev;
    const kart_t * const k = &me->race.r[0].k;
    if ( ie & ITEM_EV_HIT ) me->shake = 0.5f;
    if ( ke & KART_EV_WALL ) me->shake = fmaxf(me->shake, 0.3f);
    if ( ie & ITEM_EV_HIT ) buzz(220, 90);
    else if ( ke & KART_EV_WALL ) buzz(60, 80);
    else if ( ie & (ITEM_EV_GOT | ITEM_EV_USE) ) buzz(20, 50);
    else if ( re & RACE_EV_GO ) buzz(90, 70);
    else if ( re & RACE_EV_BUMP ) buzz(30, 60);
    else if ( ke & (KART_EV_MINITURBO | KART_EV_PAD) ) buzz(25, 60);
    else if ( ((TK_GRASS == k->surface) || (TK_SAND == k->surface)) && (40.0f < fabsf(k->speed)) && (0.0f >= k->hop) &&
             (0 <= (int32_t)(now - me->rumble_at)) )
    {
        buzz(15, 40);   /* the rough under the wheels */
        me->rumble_at = now + 160u;
    }
}

static uint32_t led_colour(const KartAo * const me, uint32_t now)
{
    const race_t * const r = &me->race;
    const racer_t * const x = &r->r[0];
    if ( RACE_COUNTING == r->phase ) return 0xC00000;
    if ( x->finished )
    {
        /* The podium's metals; no colour below third. */
        if ( 1 == x->place )
        {
            return 0xFFB000;
        }
        if ( 2 == x->place )
        {
            return 0xA0A0B0;
        }
        if ( 3 == x->place )
        {
            return 0xA05020;
        }
        return 0;
    }
    if ( 1.0f > r->clock ) return 0x00C000;
    if ( race_wrong_way(r) )
    {
        /* Wrong way: red, blinking. */
        if ( 0u != (now / 300u) % 2u )
        {
            return 0xC00000;
        }
        return 0;
    }
    if ( 0.0f < x->k.boost ) return 0xFFA000;
    switch ( kart_charge_level(&x->k) )
    {
    case 2: return 0xFF5000;
    case 1: return 0x0060FF;
    default: return 0;
    }
}

static void led(KartAo * const me, uint32_t now)
{
    const uint32_t c = led_colour(me, now);
    if ( c == me->led_rgb ) return;
    if ( KODE_OK == kode_led_rgb((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c) ) me->led_rgb = c;
}

/** @brief In a time trial, where the circuit's record ghost is now (if it was set at this engine). */
static bool trial_ghost(const KartAo * const me, float *x, float *y, float *a)
{
    const ghost_t *g = me->ghost_best;
    if ( !g || (GM_TIME_TRIAL != me->game.mode) || me->race.r[0].cpu || !g->n || (me->game.cc != g->cc) ||
        (me->game.circuit != g->circuit) )
    {
        return false;
    }
    if ( (GM_RACE != me->game.screen) && (GM_PAUSE != me->game.screen) ) return false;
    /* Through the countdown the ghost waits on the line. */
    float t = me->race.clock;
    if ( RACE_COUNTING == me->race.phase )
    {
        t = 0.0f;
    }
    return ghost_at(g, t, x, y, a);
}

/** @brief One thing standing on the track: a kart or an item, and how far ahead of the eye. */
typedef struct { float depth; int16_t kind; int16_t i; } drawable_t;

enum { DR_KART = 0, DR_BOX, DR_SEED, DR_SHOT, DR_DECOR, DR_GHOST };
#define MAX_DRAW (RACE_KARTS + ITEM_MAX_BOXES + ITEM_MAX_SEEDS + ITEM_MAX_SHOTS + TK_MAX_DECOR)
#define DRAW_FAR 520.0f

/** @brief Everything on the track, the farthest first, so the near ones cover them. */
static void draw_things(KartAo * const me, const rd_camera_t *cam, uint32_t now)
{
    const float fx = cosf(cam->angle), fy = sinf(cam->angle);
    const items_t *it = &me->race.items;
    drawable_t d[MAX_DRAW];
    int n = 0;
#define ADD(K, I, X, Y)                                                          \
    do                                                                           \
    {                                                                            \
        const float z = ((X) - cam->x) * fx + ((Y) - cam->y) * fy;               \
        if ( (4.0f < z) && (DRAW_FAR > z) )                                      \
        {                                                                        \
            d[n++] = (drawable_t){ z, (K), (int16_t)(I) };                       \
        }                                                                        \
    } while ( 0 )
    for ( int i = 0; i < me->race.n; i++ )
    {
        ADD(DR_KART, i, me->race.r[i].k.x, me->race.r[i].k.y);
    }
    for ( int i = 0; i < it->n_boxes; i++ )
    {
        if ( 0.0f >= it->boxes[i].t ) ADD(DR_BOX, i, it->boxes[i].x, it->boxes[i].y);
    }
    for ( int i = 0; i < it->n_seeds; i++ )
    {
        if ( 0.0f >= it->seeds[i].t ) ADD(DR_SEED, i, it->seeds[i].x, it->seeds[i].y);
    }
    for ( int i = 0; ITEM_MAX_SHOTS > i; i++ )
    {
        if ( IT_NONE != it->shots[i].kind ) ADD(DR_SHOT, i, it->shots[i].x, it->shots[i].y);
    }
    float gx, gy, ga;
    const bool ghost = trial_ghost(me, &gx, &gy, &ga);
    if ( ghost ) ADD(DR_GHOST, 0, gx, gy);
    for ( int i = 0; i < me->track.n_decor; i++ )
    {
        /* Only what is in front and roughly in view: the decor is the bulk of the list. */
        const tk_decor_t *dc = &me->track.decor[i];
        const float dx = dc->x - cam->x, dy = dc->y - cam->y, z = dx * fx + dy * fy;
        if ( (4.0f < z) && (DRAW_FAR > z) && ((z + 20.0f) > fabsf(dx * -fy + dy * fx)) )
        {
            ADD(DR_DECOR, i, dc->x, dc->y);
        }
    }
#undef ADD
    for ( int i = 1; i < n; i++ )
    {
        for ( int j = i; (0 < j) && (d[j - 1].depth < d[j].depth); j-- )
        {
            const drawable_t tmp = d[j];
            d[j] = d[j - 1];
            d[j - 1] = tmp;
        }
    }
    const float bob = 1.0f + 0.8f * sinf((float)now * 0.006f);
    for ( int k = 0; k < n; k++ )
    {
        const int i = d[k].i;
        switch ( d[k].kind )
        {
        case DR_KART: {
            const racer_t *x = &me->race.r[i];
            const kart_t *kk = &x->k;
            float lift = 0.0f;   /* a hop lifts the sprite */
            if ( 0.0f < kk->hop )
            {
                lift = 2.0f;
            }
            const rd_kart_fx_t f = {
                .x = kk->x, .y = kk->y, .angle = kk->angle, .lift = lift,
                .drift = kk->drift, .charge = (uint8_t)kart_charge_level(kk), .boost = 0.0f < kk->boost,
                .rough = ((TK_GRASS == kk->surface) || (TK_SAND == kk->surface)) && (30.0f < fabsf(kk->speed)),
                .star = 0.0f < kk->star, .dizzy = 0.0f < kk->spin,
            };
            rd_kart_fx(me->fb, cam, &f, x->who, now);
            break;
        }
        case DR_BOX: rd_billboard(me->fb, cam, it->boxes[i].x, it->boxes[i].y, bob, RD_BOX, 7.0f); break;
        case DR_SEED: rd_billboard(me->fb, cam, it->seeds[i].x, it->seeds[i].y, 0.5f, RD_SEED, 3.0f); break;
        case DR_DECOR: {
            static const rd_item_sprite_t SPRITE[TK_THEMES][TK_D_KINDS] = {
                { RD_TREE, RD_BUSH, RD_SIGN, RD_TYRES },
                { RD_PALM, RD_UMBRELLA, RD_SIGN, RD_TYRES },
                { RD_PINE, RD_SNOWMAN, RD_SIGN, RD_TYRES },
            };
            static const float WIDE[TK_D_KINDS] = { 14.0f, 9.0f, 16.0f, 6.0f };
            const tk_decor_t *dc = &me->track.decor[i];
            rd_billboard(me->fb, cam, dc->x, dc->y, 0.0f, SPRITE[me->track.def->theme % TK_THEMES][dc->kind],
                         WIDE[dc->kind]);
            break;
        }
        case DR_GHOST: rd_kart_ghost(me->fb, cam, gx, gy, ga); break;
        case DR_SHOT: {
            /* A shot on the track is a peel or an acorn. */
            rd_item_sprite_t what = RD_ACORN;
            float wide = 4.5f;
            if ( IT_PEEL == it->shots[i].kind )
            {
                what = RD_PEEL;
                wide = 6.0f;
            }
            rd_billboard(me->fb, cam, it->shots[i].x, it->shots[i].y, 0.0f, what, wide);
            break;
        }
        default: break;
        }
    }
}

/** @brief Wait for the last flip, paint the next frame into the back buffer, flip. */
static void flip(KartAo * const me)
{
    if ( me->in_flight )
    {
        (void)kdrv_display_flush_wait(FLUSH_WAIT_MS);
        me->in_flight = false;
    }
    uint16_t * const back = kdrv_display_back_buffer();
    if ( !back ) return;
    const int64_t u0 = esp_timer_get_time();
    upscale(me->fb, back);
    me->up_us += (uint32_t)(esp_timer_get_time() - u0);
    FlushEvt * const ev = Q_NEW_X(FlushEvt, 0U, DISPLAY_FLUSH_SIG);
    if ( !ev ) return;
    ev->x1 = 0;
    ev->y1 = 0;
    ev->x2 = SC_W;
    ev->y2 = SC_H;
    ev->pixels = (uint8_t *)back;
    ev->last = true;
    QACTIVE_POST(AO_Display, &ev->super, &me->super);
    me->in_flight = true;
}

/** @brief BOT held past HOME_HOLD_MS: home to kodeOS. Checked every frame. */
static void home_check(KartAo * const me, uint32_t now)
{
    if ( me->bot_down_ms && (HOME_HOLD_MS <= now - me->bot_down_ms) )
    {
        KLOG_I(TAG, "going home");
        kode_exit_to_os();
    }
}

/* ---- the screens ------------------------------------------------------------ */

static const uint16_t SND_TICK[] = { 1760, 20 };

/** @brief Build circuit @p which if it is not the one built. ~0.7 s on the Dot. */
static void load_circuit(KartAo * const me, int which)
{
    const tk_def_t *def = TK_ALL[which % TK_CIRCUITS];
    if ( me->track.def == def ) return;
    const int64_t b0 = esp_timer_get_time();
    track_build(&me->track, def);
    hud_minimap_build(&me->track);
    rd_theme(def->theme);
    KLOG_I(TAG, "%s: lap %d texels, built in %u ms", def->name, me->track.length,
           (unsigned)((esp_timer_get_time() - b0) / 1000));
}

/** @brief A new race: the player's (the menu's engine), or the demo behind the title, all rivals. */
static void start_race(KartAo * const me, bool demo)
{
    load_circuit(me, me->game.circuit);
    race_cc_t cc = (race_cc_t)me->game.cc;
    if ( demo )
    {
        cc = RACE_100CC;
    }
    race_init(&me->race, &me->track, cc, esp_random());
    me->race.r[0].cpu = demo;
    if ( !demo && (GM_TIME_TRIAL == me->game.mode) )
    {
        race_time_trial(&me->race);
        if ( me->ghost_rec ) ghost_start(me->ghost_rec, me->game.circuit, me->game.cc);
        if ( me->ghost_best && ghost_load(me->ghost_best, me->game.circuit) )
        {
            KLOG_I(TAG, "the ghost of %u cs at %ucc", (unsigned)me->ghost_best->time_cs, 50u * (me->ghost_best->cc + 1u));
        }
    }
    me->follow = 0;
    me->cam_angle = me->race.r[0].k.angle;
    me->done_at = 0;
    me->news = 0;
    me->acc = 0.0f;
    me->use_item = false;
    if ( demo )
    {
        KLOG_I(TAG, "the demo race");
    }
    else
    {
        KLOG_I(TAG, "a race: %s, %ucc", game_mode_name((gm_mode_t)me->game.mode), 50u * (me->game.cc + 1u));
    }
}

static void act_on(KartAo * const me, gm_action_t a)
{
    switch ( a )
    {
    case GM_START: start_race(me, false); break;
    case GM_QUIT_RACE: start_race(me, true); break;
    case GM_TICK: sound_play(SND_TICK, 1, SND_LOW); break;
    case GM_CUP_OVER: PLAY(SND_WIN, SND_HIGH); break;
    default: break;
    }
}

/** @brief The end of a race: results after a moment; the demo starts over. */
static void race_flow(KartAo * const me, uint32_t now)
{
    race_t * const r = &me->race;
    if ( (GM_TITLE == me->game.screen) || (GM_MENU == me->game.screen) || (GM_CUP == me->game.screen) )
    {
        /* The demo: the camera rides with whoever leads. */
        for ( int i = 0; i < r->n; i++ )
        {
            if ( 1 == r->r[i].place ) me->follow = (uint8_t)i;
        }
        if ( (1 <= r->finishers) && !me->done_at ) me->done_at = now;
        if ( me->done_at && (4000u < now - me->done_at) ) start_race(me, true);
        return;
    }
    if ( r->r[0].finished && !me->done_at )
    {
        me->done_at = now;
        const racer_t *x = &r->r[0];
        if ( me->ghost_rec && (1 == r->n) )
        {
            me->ghost_rec->time_cs = (uint32_t)(x->finish_time * 100.0f);
        }
        me->news = records_add(&me->records, me->game.circuit, game_record_mode(&me->game), me->game.cc, x->finish_time, x->best_lap,
                               x->place, r->n);
        const char *record = "";
        if ( me->news )
        {
            record = ", a record";
        }
        KLOG_I(TAG, "finished %d of %d in %u cs%s", x->place, r->n, (unsigned)(x->finish_time * 100.0f), record);
        if ( !records_save(&me->records) ) KLOG_W(TAG, "the records were not saved");
        /* A time trial that beat the circuit's record (at any engine) leaves its ghost. */
        if ( me->ghost_rec && me->ghost_best && (1 == r->n) && (me->news & REC_NEW_RACE) )
        {
            memcpy(me->ghost_best, me->ghost_rec, GHOST_BYTES(me->ghost_rec));
            if ( !ghost_save(me->ghost_best) ) KLOG_W(TAG, "the ghost was not saved");
        }
    }
    if ( (GM_RACE == me->game.screen) && me->done_at && (2500u < now - me->done_at) )
    {
        uint8_t places[RACE_KARTS];
        for ( int i = 0; i < r->n; i++ )
        {
            places[i] = r->r[i].place;
        }
        game_race_over(&me->game, places, r->n);
    }
}

/** @brief What the console reads, refreshed every frame. */
static void snapshot(const KartAo * const me)
{
    const race_t * const r = &me->race;
    const racer_t * const x = &r->r[0];
    volatile kk_snapshot_t * const s = &g_kk_snapshot;
    s->screen = me->game.screen;
    s->mode = me->game.mode;
    s->cc = me->game.cc;
    s->place = x->place;
    s->racers = r->n;
    /* Each field is worked out in a local and written once: the console may read it meanwhile. */
    uint8_t lap = (uint8_t)x->lap;   /* clamped to 1..laps */
    if ( 1 > x->lap )
    {
        lap = 1;
    }
    else if ( r->laps < x->lap )
    {
        lap = r->laps;
    }
    s->lap = lap;
    s->laps = r->laps;
    s->seeds = x->k.seeds;
    uint8_t item = x->item;   /* nothing in hand while the roulette spins */
    if ( 0.0f < x->roll )
    {
        item = IT_NONE;
    }
    s->item = item;
    s->speed = (uint16_t)fabsf(x->k.speed);
    uint32_t clock_cs = 0;   /* the finish time once finished, 0 before GO */
    if ( 0.0f < r->clock )
    {
        float clock_s = r->clock;
        if ( x->finished )
        {
            clock_s = x->finish_time;
        }
        clock_cs = (uint32_t)(clock_s * 100.0f);
    }
    s->clock_cs = clock_cs;
    s->races = me->records.races;
    s->wins = me->records.wins;
    s->podiums = me->records.podiums;
    const int c = me->game.circuit % REC_CIRCUITS, m = game_record_mode(&me->game), e = me->game.cc % REC_CCS;
    s->best_race_cs = me->records.best_race_cs[c][m][e];
    s->best_lap_cs = me->records.best_lap_cs[c][m][e];
    s->fps = me->fps;
}

/* ---- the frame -------------------------------------------------------------- */

static void frame(KartAo * const me)
{
    const int64_t t0 = esp_timer_get_time();
    home_check(me, kode_time_ms());
    const uint32_t t_ms = kode_time_ms();
    step(me, t_ms);
    race_flow(me, t_ms);
    rd_camera_t cam;
    chase_camera(me, &cam, t_ms);
    rd_sky(me->fb, &cam, t_ms);
    rd_ground(me->fb, &me->track, &cam);
    draw_things(me, &cam, t_ms);
    rd_weather(me->fb, &cam, t_ms);
    const bool playing = (GM_RACE == me->game.screen) || (GM_PAUSE == me->game.screen) ||
                         (GM_RESULTS == me->game.screen) || (GM_CUP == me->game.screen);
    switch ( me->game.screen )
    {
    case GM_TITLE: screens_title(me->fb, t_ms); break;
    case GM_MENU: screens_menu(me->fb, &me->game, &me->records, t_ms); break;
    case GM_RACE: hud_draw(me->fb, &me->race, t_ms); break;
    case GM_PAUSE: hud_draw(me->fb, &me->race, t_ms); screens_pause(me->fb); break;
    case GM_RESULTS: screens_results(me->fb, &me->race, &me->game, me->news, t_ms); break;
    case GM_CUP: screens_cup(me->fb, &me->game, t_ms); break;
    default: break;
    }
    if ( GM_RACE == me->game.screen )
    {
        sounds(me);
        feel(me, t_ms);
    }
    /* BOT held: a bar fills toward home, so a hold is not a mystery. */
    if ( me->bot_down_ms && (250u < t_ms - me->bot_down_ms) )
    {
        const int w = (int)((t_ms - me->bot_down_ms) * 100u / HOME_HOLD_MS);
        const int x0 = RD_W / 2 - 50, y0 = RD_H / 2 - 4;
        for ( int y = y0 - 1; y0 + 9 > y; y++ )
        {
            for ( int x = x0 - 1; x0 + 101 > x; x++ )
            {
                const bool in = (y0 <= y) && (y0 + 8 > y) && (x0 <= x) && (x0 + 100 > x);
                /* An ink frame; inside, yellow up to w percent, gray after. */
                uint16_t px = RGB565(0x404048);
                if ( !in )
                {
                    px = RGB565(PAL_INK);
                }
                else if ( (x - x0) < w )
                {
                    px = RGB565(PAL_YELLOW);
                }
                me->fb[y * RD_W + x] = px;
            }
        }
        font_draw_centred(me->fb, RD_W, RD_H, RD_W / 2, y0 - 14, "HOME", 1, RGB565(PAL_WHITE), RGB565(PAL_INK));
    }
    if ( playing )
    {
        led(me, t_ms);
    }
    else if ( (0 != me->led_rgb) && (KODE_OK == kode_led_rgb(0, 0, 0)) )
    {
        me->led_rgb = 0;
    }
    me->race_ev = me->kart_ev = me->item_ev = 0;
    sound_step(kode_time_ms());
    snapshot(me);
    me->busy_us += (uint32_t)(esp_timer_get_time() - t0);
    flip(me);
    const uint32_t now = kode_time_ms();
    me->fps_n++;
    if ( FPS_EVERY_MS <= now - me->fps_t0 )
    {
        /* Every few seconds, not every one: a log line can cut a screenshot's record on the cable. */
        KLOG_I(TAG, "fps %u, paint %u us, double %u us a frame", (unsigned)(me->fps_n * 1000u / FPS_EVERY_MS),
               (unsigned)(me->busy_us / me->fps_n), (unsigned)(me->up_us / me->fps_n));
        me->up_us = 0;
        me->fps = (uint8_t)(me->fps_n * 1000u / FPS_EVERY_MS);
        me->fps_n = 0;
        me->busy_us = 0;
        me->fps_t0 = now;
    }
    vTaskDelay(1);
    QACTIVE_POST(&me->super, &FRAME_EVT, &me->super);
}

/* ---- the AO ----------------------------------------------------------------- */

void KartAo_ctor(void)
{
    KartAo * const me = &l_ao;
    QActive_ctor(&me->super, Q_STATE_CAST(&KartAo_initial));
    me->led_rgb = 0xFFFFFFFFu;
}

static volatile uint8_t s_circuit = 0xFF;   /* the console's choice, picked up on the next start */

void kart_ao_circuit(uint8_t circuit) { s_circuit = circuit; }

bool kart_ao_start(uint8_t mode, uint8_t cc)
{
    if ( s_start.busy ) return false;
    s_start.mode = mode;
    s_start.cc = cc;
    s_start.busy = 1;
    if ( !QACTIVE_POST_X(AO_Kart, &START_EVT, 1U, (void *)0) )
    {
        s_start.busy = 0;
        return false;
    }
    return true;
}

void KartAo_start(void)
{
    QActive_setAttr(AO_Kart, TASK_NAME_ATTR, "kart_ao");
    QACTIVE_START(AO_Kart, KODE_PRIO_USER_GUI, l_queue, Q_DIM(l_queue), l_stack, sizeof(l_stack),
                  (void *)0);
}

static QState KartAo_initial(KartAo * const me, void const * const par)
{
    (void)par;
    me->fb = heap_caps_malloc((size_t)FB_W * FB_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    if ( !me->fb ) me->fb = heap_caps_malloc((size_t)FB_W * FB_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if ( !me->fb ) KLOG_E(TAG, "no frame buffer: nothing will show");
    track_t * const t = &me->track;
    t->tex = heap_caps_malloc((size_t)TK_SIZE * TK_SIZE, MALLOC_CAP_SPIRAM);
    t->prog = heap_caps_malloc((size_t)TK_SIZE * TK_SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    t->off = heap_caps_malloc((size_t)TK_SIZE * TK_SIZE, MALLOC_CAP_SPIRAM);
    t->line = heap_caps_malloc(sizeof(tk_line_t) * TK_MAX_LINE, MALLOC_CAP_SPIRAM);
    rd_init();
    me->ghost_rec = heap_caps_calloc(1, sizeof(ghost_t), MALLOC_CAP_SPIRAM);
    me->ghost_best = heap_caps_calloc(1, sizeof(ghost_t), MALLOC_CAP_SPIRAM);
    if ( !t->tex || !t->prog || !t->off || !t->line )
    {
        KLOG_E(TAG, "no PSRAM for the circuit");
        me->fb = NULL;
    }
    else
    {
        t->def = NULL;
        game_init(&me->game);
        records_load(&me->records);
        start_race(me, true);
    }
    QActive_subscribe(&me->super, BUTTON_EVT_SIG);
    QActive_subscribe(&me->super, DISPLAY_STATE_SIG);
    QActive_subscribe(&me->super, TOUCH_EVT_SIG);
    /* The panel was announced from inside kode_init(), before this AO existed. */
    display_state_request();
    return Q_TRAN(&KartAo_waiting);
}

/** @brief Parent of every state: BOT tapped uses the item, held goes home. */
static QState KartAo_running(KartAo * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case BUTTON_EVT_SIG: {
        ButtonEvt const * const be = (ButtonEvt const *)e;
        const uint32_t now = kode_time_ms();
        if ( (uint8_t)KDRV_BTN_BOT == be->id )
        {
            /* BOT acts on its release, so a hold can mean home instead. */
            if ( be->pressed )
            {
                /* 0 means "up": a press at 0 ms is kept as 1. */
                uint32_t down = now;
                if ( 0u == now )
                {
                    down = 1;
                }
                me->bot_down_ms = down;
            }
            else
            {
                if ( me->bot_down_ms && (HOME_HOLD_MS > now - me->bot_down_ms) )
                {
                    if ( GM_RACE == me->game.screen )
                    {
                        me->use_item = true;
                    }
                    else
                    {
                        act_on(me, game_press(&me->game, GM_B_BOT));
                    }
                }
                me->bot_down_ms = 0;
            }
            return Q_HANDLED();
        }
        static const gm_button_t MAP[KDRV_BTN_COUNT] = {
            [KDRV_BTN_UP] = GM_B_UP, [KDRV_BTN_DOWN] = GM_B_DOWN, [KDRV_BTN_LEFT] = GM_B_LEFT,
            [KDRV_BTN_RIGHT] = GM_B_RIGHT, [KDRV_BTN_TOP] = GM_B_TOP,
        };
        if ( be->pressed && (KDRV_BTN_COUNT > be->id) ) act_on(me, game_press(&me->game, MAP[be->id]));
        return Q_HANDLED();
    }
    case KART_START_SIG:
        s_start.busy = 0;   /* no glass yet: the order is dropped, the mailbox freed */
        return Q_HANDLED();
    default:
        return Q_SUPER(&QHsm_top);
    }
}

static QState KartAo_waiting(KartAo * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case DISPLAY_STATE_SIG: {
        DisplayStateEvt const * const st = (DisplayStateEvt const *)e;
        if ( me->fb && st->up ) return Q_TRAN(&KartAo_racing);
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&KartAo_running);
    }
}

static QState KartAo_racing(KartAo * const me, QEvt const * const e)
{
    switch ( e->sig )
    {
    case Q_ENTRY_SIG:
        me->fps_t0 = me->last_ms = kode_time_ms();
        QACTIVE_POST(&me->super, &FRAME_EVT, &me->super);
        return Q_HANDLED();
    case KART_FRAME_SIG:
        frame(me);
        return Q_HANDLED();
    case KART_START_SIG:
        if ( TK_CIRCUITS > s_circuit ) me->game.circuit = s_circuit;
        s_circuit = 0xFF;
        me->game.mode = s_start.mode;
        me->game.cc = s_start.cc;
        s_start.busy = 0;
        me->game.screen = GM_RACE;
        start_race(me, false);
        return Q_HANDLED();
    case TOUCH_EVT_SIG: {
        TouchEvt const * const te = (TouchEvt const *)e;
        if ( te->pressed && !me->touching ) act_on(me, game_press(&me->game, GM_B_TOUCH));
        me->touching = te->pressed;
        return Q_HANDLED();
    }
    default:
        return Q_SUPER(&KartAo_running);
    }
}
