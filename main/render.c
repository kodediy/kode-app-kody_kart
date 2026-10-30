/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file render.c
 * @brief Mode 7: a flat textured world seen in perspective, one row at a time.
 *
 * Every ground row is a straight line across the world: the row at dy pixels
 * under the horizon lies z = height * focal / dy texels ahead, and its pixels
 * step across the world by z / focal texels each. So a row costs one division
 * and then two additions per pixel, in 16.16 fixed point. Far rows are hazed
 * toward the sky's colour by picking one of RD_FOG ground tables.
 */
#include "render.h"

#include <math.h>
#include <stdbool.h>

#include "palette.h"
#include "sprites_gen.h"

#define RD_FOG 4
#define PANO   720     /* the sky's panorama, pixels round: about 2 pi x the focal */

static uint16_t s_ground[RD_FOG][TK_C_COUNT];
static const uint32_t HAZE = 0xBFE4F8;
static const uint32_t SKY_TOP = 0x3C8CE8;

static uint16_t s_sky[RD_HORIZON];
static uint8_t  s_far[PANO], s_near[PANO];
static uint16_t s_far_c, s_near_c, s_tree_c, s_cloud_c, s_cloud_shade;
static uint8_t  s_cloud_bot[PANO], s_cloud_h[PANO];   /* clouds_build() */
static void     clouds_build(void);
static bool     s_sun;   /* the beach's sun, at a fixed place on the panorama */

#define SUN_P   300      /* where on the panorama */
#define SUN_ROW 16
#define SUN_R   7

/** @brief Clouds on the panorama: where, how high their flat bottom is, how wide. */
static const struct { int16_t x; int8_t y; uint8_t w; } CLOUDS[] = {
    { 40, 12, 22 }, { 190, 7, 30 }, { 330, 15, 18 }, { 470, 9, 26 }, { 610, 13, 20 },
};

/* Kody and his cousins: a kart colour and a fur colour each. */
const rd_character_t RD_CAST[RD_CHARACTERS] = {
    { "KODY",  { 0xFFD060, 0xFFAC00, 0xC07A00 }, { 0x3DB0E0, 0x028AC2, 0x01628A } },
    { "ROSA",  { 0xF4F4F4, 0xD8D8E0, 0xA0A0B0 }, { 0xF59AC4, 0xE0609C, 0xA83C70 } },
    { "LIMA",  { 0xF06A6A, 0xC9303D, 0x8A1E28 }, { 0x9CE07A, 0x62B843, 0x3E8228 } },
    { "CHOCO", { 0x7AB8F0, 0x3C7FD0, 0x24548E }, { 0xB8865E, 0x8A5A3C, 0x5E3A24 } },
    { "NUBE",  { 0xC8A0F0, 0x9058D0, 0x5E3494 }, { 0xFFFFFF, 0xDCDCE6, 0xA8A8B8 } },
    { "TANGO", { 0x7AE0A8, 0x2EB872, 0x1C7A4A }, { 0xFFC070, 0xF08A24, 0xB05E10 } },
};

/* Each character's sprite palette, RGB565, built once; the items' one. */
static uint16_t s_cast[RD_CHARACTERS][SPR_COLOURS];
static uint16_t s_items[ITEM_COLOURS];

static const item_sprite_t *const ITEM_SPRITES[RD_ITEM_SPRITES] = {
    [RD_BOX] = &SPR_BOX, [RD_SEED] = &SPR_SEED, [RD_GOLDEN] = &SPR_GOLDEN,
    [RD_PEEL] = &SPR_PEEL, [RD_ACORN] = &SPR_ACORN, [RD_TURBO] = &SPR_TURBO,
    [RD_TREE] = &SPR_TREE, [RD_BUSH] = &SPR_BUSH, [RD_SIGN] = &SPR_SIGN, [RD_TYRES] = &SPR_TYRES,
    [RD_PALM] = &SPR_PALM, [RD_UMBRELLA] = &SPR_UMBRELLA, [RD_PINE] = &SPR_PINE, [RD_SNOWMAN] = &SPR_SNOWMAN,
};

static uint32_t mix(uint32_t a, uint32_t b, int num, int den)
{
    uint32_t out = 0;
    for ( int sh = 0; 16 >= sh; sh += 8 )
    {
        const int ca = (int)(a >> sh & 255), cb = (int)(b >> sh & 255);
        out |= (uint32_t)(ca + (cb - ca) * num / den) << sh;
    }
    return out;
}

/** @brief A ridge line that wraps round the panorama: whole sine periods only. */
static void ridge(uint8_t *h, int base, const int *freq, const float *amp, const float *phase, int n)
{
    for ( int i = 0; PANO > i; i++ )
    {
        float v = (float)base;
        for ( int k = 0; n > k; k++ )
        {
            v += amp[k] * sinf(2.0f * (float)M_PI * freq[k] * i / PANO + phase[k]);
        }
        if ( 0 > v )
        {
            v = 0;
        }
        h[i] = (uint8_t)v;
    }
}

/** @brief The sky for a theme: the park's mountains and wooded hills, or the beach's islands under a sun. */
static void sky_theme(int theme)
{
    const bool beach = TK_THEME_BEACH == theme, snow = TK_THEME_SNOW == theme;
    /* The park's colours unless the theme says otherwise. */
    uint32_t top = SKY_TOP, haze = HAZE, far_c = 0x7FA6C8, near_c = 0x3E8A3A, tree_c = 0x2C6E2E;
    if ( snow )
    {
        /* Tall white peaks far off, dark pines near. */
        static const int ff[] = { 2, 4, 9, 19 };
        static const float fa[] = { 9.0f, 6.0f, 3.0f, 1.5f }, fp[] = { 2.3f, 0.9f, 4.7f, 1.6f };
        ridge(s_far, 20, ff, fa, fp, 4);
        static const int nf[] = { 3, 9, 19, 53 };
        static const float na[] = { 2.0f, 1.5f, 1.0f, 2.5f }, np[] = { 1.9f, 3.4f, 0.8f, 2.6f };
        ridge(s_near, 6, nf, na, np, 4);
    }
    else if ( beach )
    {
        /* Low islands far off, almost nothing near: the sea is the horizon. */
        static const int ff[] = { 3, 7, 13, 29 };
        static const float fa[] = { 5.0f, 3.0f, 1.5f, 0.6f }, fp[] = { 1.1f, 0.2f, 3.3f, 4.4f };
        ridge(s_far, 1, ff, fa, fp, 4);
        static const int nf[] = { 5, 11, 23, 47 };
        static const float na[] = { 1.2f, 0.8f, 0.6f, 0.5f }, np[] = { 0.7f, 2.1f, 1.4f, 3.9f };
        ridge(s_near, 0, nf, na, np, 4);
    }
    else
    {
        static const int ff[] = { 2, 5, 11, 23 };
        static const float fa[] = { 6.0f, 4.0f, 2.0f, 1.0f }, fp[] = { 0.3f, 1.7f, 4.1f, 2.2f };
        ridge(s_far, 13, ff, fa, fp, 4);
        static const int nf[] = { 3, 7, 17, 41 };
        static const float na[] = { 2.5f, 1.5f, 1.0f, 1.5f }, np[] = { 2.9f, 0.4f, 5.3f, 1.1f };
        ridge(s_near, 5, nf, na, np, 4);
    }
    if ( beach )
    {
        top = 0x1FA0E8;
        haze = 0xD8F2FA;
        far_c = 0x5FA8A0;
        near_c = 0x3E9A5A;
        tree_c = 0x2C7A40;
    }
    else if ( snow )
    {
        top = 0x7C9CC8;
        haze = 0xE8EEF6;
        far_c = 0xF4F8FC;
        near_c = 0x2E5A44;
        tree_c = 0xE8F0F6;
    }
    for ( int r = 0; RD_HORIZON > r; r++ )
    {
        s_sky[r] = RGB565(mix(top, haze, r, RD_HORIZON));
    }
    s_far_c = RGB565(far_c);
    s_near_c = RGB565(near_c);
    s_tree_c = RGB565(tree_c);
    s_sun = beach;
}

void rd_init(void)
{
    s_cloud_c = RGB565(0xFFFFFF);
    s_cloud_shade = RGB565(0xDCEAF6);
    clouds_build();
    rd_theme(TK_THEME_PARK);
    for ( int c = 1; ITEM_COLOURS > c; c++ )
    {
        s_items[c] = RGB565(ITEM_PAL[c]);
    }
    for ( int k = 0; RD_CHARACTERS > k; k++ )
    {
        uint16_t *p = s_cast[k];
        p[SPR_INK] = RGB565(PAL_INK);
        p[SPR_WHITE] = RGB565(PAL_WHITE);
        p[SPR_GRAY] = RGB565(0x9A9AA4);
        p[SPR_DARK] = RGB565(0x555560);
        for ( int t = 0; 3 > t; t++ )
        {
            p[SPR_BODY_L + t] = RGB565(RD_CAST[k].body[t]);
            p[SPR_FUR_L + t] = RGB565(RD_CAST[k].fur[t]);
        }
    }
}

static int s_theme;

void rd_theme(int theme)
{
    s_theme = theme;
    sky_theme(theme);
    for ( int f = 0; RD_FOG > f; f++ )
    {
        for ( int c = 0; TK_C_COUNT > c; c++ )
        {
            s_ground[f][c] = RGB565(mix(pal_ground(theme, c), HAZE, f, RD_FOG + 1));
        }
    }
}

void rd_weather(uint16_t *fb, const rd_camera_t *cam, uint32_t now_ms)
{
    /* The water shimmers: its two tones trade places a few times a second. */
    static int phase = -1;
    const int now_phase = (int)((now_ms / 450u) & 1u);
    if ( now_phase != phase )
    {
        phase = now_phase;
        static const int WET[][2] = { { TK_C_WATER, TK_C_WATER2 }, { TK_C_FOREST, TK_C_FOREST2 } };
        int n = 1;
        if ( TK_THEME_BEACH == s_theme )
        {
            n = 2;   /* the beach's sea is its "forest" */
        }
        if ( TK_THEME_SNOW != s_theme )
        {
            for ( int w = 0; n > w; w++ )
            {
                for ( int f = 0; RD_FOG > f; f++ )
                {
                    const int a = WET[w][phase], b = WET[w][!phase];
                    s_ground[f][WET[w][0]] = RGB565(mix(pal_ground(s_theme, a), HAZE, f, RD_FOG + 1));
                    s_ground[f][WET[w][1]] = RGB565(mix(pal_ground(s_theme, b), HAZE, f, RD_FOG + 1));
                }
            }
        }
    }
    if ( TK_THEME_SNOW != s_theme )
    {
        return;
    }
    /* Snow: flakes on a slowly falling, slightly swaying lattice that turns with the eye. */
    const uint16_t flake = RGB565(0xFFFFFF);
    const int turn = (int)lrintf(cam->angle * (float)PANO / (2.0f * (float)M_PI));
    for ( int i = 0; 70 > i; i++ )
    {
        const uint32_t h = (uint32_t)i * 2654435761u;
        const int speed = 18 + (int)(h % 23);                     /* px a second */
        const int x0 = (int)((h >> 8) % (RD_W + 40)) - 20;
        const int y = (int)(((h >> 16) % RD_H + now_ms * (uint32_t)speed / 1000u) % RD_H);
        const int sway = (int)(3.0f * sinf((float)now_ms * 0.002f + (float)i));
        int x = (x0 - turn * (1 + i % 2) + sway) % (RD_W + 40);
        if ( 0 > x )
        {
            x += RD_W + 40;
        }
        x -= 20;
        if ( RD_W <= (unsigned)x )
        {
            continue;
        }
        fb[y * RD_W + x] = flake;
        if ( (0 == i % 3) && (RD_W > x + 1) )
        {
            fb[y * RD_W + x + 1] = flake;   /* the nearer flakes are bigger */
        }
    }
}

/**
 * @brief The clouds' silhouette per panorama column, drawn once: the row of
 *        the flat bottom and how many rows they rise (0 = no cloud there).
 */
static void clouds_build(void)
{
    for ( int p = 0; PANO > p; p++ )
    {
        s_cloud_h[p] = 0;
    }
    for ( unsigned k = 0; sizeof CLOUDS / sizeof CLOUDS[0] > k; k++ )
    {
        const int w = CLOUDS[k].w, h = w / 4;
        for ( int dx = 0; w > dx; dx++ )
        {
            /* Three bumps on a flat bottom. */
            int bump = (int)(h * sinf((float)M_PI * dx / w));
            if ( 1 == (dx * 3 / w) )
            {
                bump += 2;
            }
            const int p = (CLOUDS[k].x + dx) % PANO;
            if ( bump > s_cloud_h[p] )
            {
                s_cloud_h[p] = (uint8_t)bump;
                s_cloud_bot[p] = (uint8_t)CLOUDS[k].y;
            }
        }
    }
}

void rd_sky(uint16_t *fb, const rd_camera_t *cam, uint32_t now_ms)
{
    /* A full turn is the whole panorama, so it never jumps; close enough to
     * focal pixels a radian that the hills turn with the ground. */
    int off = (int)lrintf(cam->angle * (float)PANO / (2.0f * (float)M_PI)) % PANO;
    if ( 0 > off )
    {
        off += PANO;
    }
    const int drift = (int)(now_ms / 400u) % PANO;
    const uint16_t sun_rim = RGB565(0xFFD84A), sun_core = RGB565(0xFFF4B0);
    for ( int i = 0; RD_W > i; i++ )
    {
        const int p = (off + i) % PANO;
        const int far = s_far[p], near = s_near[p];
        int cp = p - drift;
        if ( 0 > cp )
        {
            cp += PANO;
        }
        const int cbot = s_cloud_bot[cp], ctop = cbot - s_cloud_h[cp];   /* cloud rows: (ctop, cbot] */
        int sdx = PANO;
        if ( s_sun )
        {
            sdx = p - SUN_P;
            if ( PANO / 2 < sdx )
            {
                sdx -= PANO;
            }
            if ( -PANO / 2 > sdx )
            {
                sdx += PANO;
            }
        }
        const bool sun_col = SUN_R * SUN_R >= sdx * sdx;
        uint16_t *out = fb + i;
        for ( int r = 0; RD_HORIZON > r; r++, out += RD_W )
        {
            const int up = RD_HORIZON - 1 - r;
            uint16_t c;
            if ( near > up )
            {
                /* One near column in five is a tree. */
                if ( 0 == ((p * 7 + up) % 5) )
                {
                    c = s_tree_c;
                }
                else
                {
                    c = s_near_c;
                }
            }
            else if ( far > up )
            {
                c = s_far_c;
            }
            else if ( (cbot >= r) && (ctop < r) )
            {
                /* The cloud's bottom row is its shade. */
                if ( cbot == r )
                {
                    c = s_cloud_shade;
                }
                else
                {
                    c = s_cloud_c;
                }
            }
            else
            {
                c = s_sky[r];
                if ( sun_col )
                {
                    const int d2 = sdx * sdx + (r - SUN_ROW) * (r - SUN_ROW);
                    if ( SUN_R * SUN_R >= d2 )
                    {
                        if ( (SUN_R - 2) * (SUN_R - 2) >= d2 )
                        {
                            c = sun_core;
                        }
                        else
                        {
                            c = sun_rim;
                        }
                    }
                }
            }
            *out = c;
        }
    }
}

void rd_ground(uint16_t *fb, const track_t *t, const rd_camera_t *cam)
{
    const float fx = cosf(cam->angle), fy = sinf(cam->angle);
    const float rx = -fy, ry = fx;   /* right, in a y-down world */
    const uint8_t *tex = t->tex;
    for ( int row = RD_HORIZON; RD_VIEW_H > row; row++ )
    {
        const float dy = (float)(row - RD_HORIZON) + 0.5f;
        const float z = cam->height * cam->focal / dy;
        const float step = z / cam->focal;
        const float half = 0.5f * (float)RD_W - 0.5f;
        /* The row's leftmost pixel, and how far one pixel to the right moves. */
        int32_t x = (int32_t)((cam->x + fx * z - rx * step * half) * 65536.0f);
        int32_t y = (int32_t)((cam->y + fy * z - ry * step * half) * 65536.0f);
        const int32_t sx = (int32_t)(rx * step * 65536.0f), sy = (int32_t)(ry * step * 65536.0f);
        /* The farther the row, the hazier its table. */
        int fog = 0;
        if ( 520.0f < z )
        {
            fog = 3;
        }
        else if ( 340.0f < z )
        {
            fog = 2;
        }
        else if ( 200.0f < z )
        {
            fog = 1;
        }
        const uint16_t *pal = s_ground[fog];
        uint16_t *out = fb + row * RD_W;
        for ( int i = 0; RD_W > i; i++ )
        {
            const uint32_t tx = (uint32_t)(x >> 16), ty = (uint32_t)(y >> 16);
            /* Off the world is forest. */
            uint8_t c;
            if ( TK_SIZE > (tx | ty) )
            {
                c = tex[(ty << 10) | tx];
            }
            else
            {
                c = TK_C_FOREST;
            }
            out[i] = pal[c];
            x += sx;
            y += sy;
        }
    }
}

int rd_project(const rd_camera_t *cam, float wx, float wy, float *sx, float *sy, float *scale)
{
    const float fx = cosf(cam->angle), fy = sinf(cam->angle);
    const float dx = wx - cam->x, dy = wy - cam->y;
    const float z = dx * fx + dy * fy;          /* ahead */
    const float lat = dx * -fy + dy * fx;       /* to the right */
    if ( 4.0f > z )
    {
        return 0;
    }
    *scale = cam->focal / z;
    *sx = 0.5f * (float)RD_W + lat * *scale;
    *sy = (float)RD_HORIZON + cam->height * *scale;
    return 1;
}

/** @brief Half as bright: the shadow under a kart. */
static inline uint16_t darken(uint16_t c)
{
    return (uint16_t)((c >> 1) & 0x7BEF);
}

static void kart_draw(uint16_t *fb, const rd_camera_t *cam, float x, float y, float angle, float lift,
                      const uint16_t *pal, bool ghost);

void rd_kart(uint16_t *fb, const rd_camera_t *cam, float x, float y, float angle, float lift, int who)
{
    kart_draw(fb, cam, x, y, angle, lift, s_cast[who % RD_CHARACTERS], false);
}

void rd_kart_ghost(uint16_t *fb, const rd_camera_t *cam, float x, float y, float angle)
{
    static uint16_t pale[SPR_COLOURS];
    if ( !pale[SPR_INK] )
    {
        for ( int c = 1; SPR_COLOURS > c; c++ )
        {
            pale[c] = RGB565(0xC8E4F8);
        }
        pale[SPR_INK] = RGB565(0x6A8AA8);
        pale[SPR_BODY_D] = pale[SPR_FUR_D] = RGB565(0x9CC0E0);
    }
    kart_draw(fb, cam, x, y, angle, 0.0f, pale, true);
}

/** @brief A square blob of @p r screen pixels at world (wx, wy) raised @p up texels. */
static void blob(uint16_t *fb, const rd_camera_t *cam, float wx, float wy, float up, float r, uint16_t c)
{
    float sx, sy, sc;
    if ( !rd_project(cam, wx, wy, &sx, &sy, &sc) )
    {
        return;
    }
    int rr = (int)(r * sc / 3.0f + 0.5f);
    if ( 0 >= rr )
    {
        rr = 1;
    }
    const int cx = (int)sx, cy = (int)(sy - up * sc);
    for ( int y = cy - rr; cy >= y; y++ )
    {
        for ( int x = cx - rr / 2; cx + rr / 2 >= x; x++ )
        {
            if ( (RD_W > (unsigned)x) && (RD_VIEW_H > (unsigned)y) )
            {
                fb[y * RD_W + x] = c;
            }
        }
    }
}

static uint32_t fx_hash(uint32_t a)
{
    a = (a ^ 61u) ^ (a >> 16);
    a *= 9u;
    a ^= a >> 4;
    a *= 0x27d4eb2du;
    return a ^ (a >> 15);
}

void rd_kart_fx(uint16_t *fb, const rd_camera_t *cam, const rd_kart_fx_t *k, int who, uint32_t now_ms)
{
    const float fx = cosf(k->angle), fy = sinf(k->angle), rx = -fy, ry = fx;
    const uint32_t tick = now_ms / 50u;
    /* Dust first: it rises behind the kart. */
    if ( k->rough )
    {
        for ( int i = 0; 4 > i; i++ )
        {
            const uint32_t h = fx_hash(tick * 7u + (uint32_t)i + (uint32_t)who * 101u);
            const float back = 5.0f + (float)(h % 5), side = ((float)(h >> 4 & 7) - 3.5f);
            uint16_t dust = RGB565(0xA89470);
            if ( (h >> 12) & 1 )
            {
                dust = RGB565(0xC8B48C);
            }
            blob(fb, cam, k->x - fx * back + rx * side, k->y - fy * back + ry * side, (float)(h >> 8 & 3), 1.6f, dust);
        }
    }
    if ( k->star )
    {
        /* Gold and the character's own colours, alternating fast. */
        static uint16_t gold[SPR_COLOURS];
        if ( !gold[SPR_INK] )
        {
            for ( int c = 0; SPR_COLOURS > c; c++ )
            {
                gold[c] = s_cast[0][c];
            }
            for ( int t = 0; 3 > t; t++ )
            {
                gold[SPR_BODY_L + t] = gold[SPR_FUR_L + t] = RGB565(0xFFE070 - 0x181800 * (uint32_t)t);
            }
        }
        const uint16_t *pal = s_cast[who % RD_CHARACTERS];
        if ( tick & 1 )
        {
            pal = gold;
        }
        kart_draw(fb, cam, k->x, k->y, k->angle, k->lift, pal, false);
    }
    else
    {
        rd_kart(fb, cam, k->x, k->y, k->angle, k->lift, who);
    }
    /* Flames out of the exhausts. */
    if ( k->boost )
    {
        for ( int side = -1; 1 >= side; side += 2 )
        {
            const uint32_t h = fx_hash(tick + (uint32_t)side * 13u);
            const float len = 4.0f + (float)(h % 3);
            uint16_t flame = RGB565(0xFF6A00);
            if ( h & 1 )
            {
                flame = RGB565(0xFFD040);
            }
            blob(fb, cam, k->x - fx * len + rx * 1.0f * side, k->y - fy * len + ry * 1.0f * side, 1.6f + k->lift, 1.8f,
                 flame);
        }
    }
    /* Spun out: three little stars circling over the driver's head. */
    if ( k->dizzy )
    {
        for ( int i = 0; 3 > i; i++ )
        {
            const float a = (float)now_ms * 0.008f + (float)i * 2.094f;
            uint16_t star = RGB565(0xFFD84A);
            if ( 1 == i )
            {
                star = RGB565(0xFFFFFF);
            }
            blob(fb, cam, k->x + cosf(a) * 3.5f, k->y + sinf(a) * 3.5f, 7.0f + k->lift + 0.6f * sinf(a * 2.0f), 1.3f,
                 star);
        }
    }
    /* Sparks at the rear wheels while drifting: white, then the charge's colour. */
    if ( k->drift )
    {
        uint16_t c = RGB565(0xF0F0F0);
        if ( 2 == k->charge )
        {
            c = RGB565(0xFF7A10);
        }
        else if ( 1 == k->charge )
        {
            c = RGB565(0x40A0FF);
        }
        for ( int side = -1; 1 >= side; side += 2 )
        {
            for ( int i = 0; 4 > i; i++ )
            {
                const uint32_t h = fx_hash(tick * 3u + (uint32_t)i * 17u + (uint32_t)(side + 1) * 5u);
                if ( 0 == (h & 3) )
                {
                    continue;
                }
                const float back = 3.5f + (float)(h >> 2 & 3), wide = 3.6f + (float)(h >> 4 & 1);
                blob(fb, cam, k->x - fx * back + rx * wide * side, k->y - fy * back + ry * wide * side,
                     (float)(h >> 6 & 1) + k->lift, 1.4f + 0.8f * (float)(k->charge), c);
            }
        }
    }
}

static void kart_draw(uint16_t *fb, const rd_camera_t *cam, float x, float y, float angle, float lift,
                      const uint16_t *pal, bool ghost)
{
    float sx, sy, sc;
    if ( !rd_project(cam, x, y, &sx, &sy, &sc) )
    {
        return;
    }
    const float k = sc / RD_SPRITE_PER_TEXEL;          /* screen pixels per sprite pixel */
    if ( 0.12f > k )
    {
        return;                                        /* a dot on the horizon: skip it */
    }
    /* Which view: view i is seen by an eye turned i slices to the kart's left
     * (gen-sprites.mjs), so it is the kart's heading minus the eye's. A kart
     * steering right of the eye shows its nose to the right. */
    float rel = angle - cam->angle;
    const float slice = 2.0f * (float)M_PI / KART_VIEWS;
    int view = (int)lrintf(rel / slice) % KART_VIEWS;
    if ( 0 > view )
    {
        view += KART_VIEWS;
    }
    const uint8_t *spr = KART_PX[view];
    const float up = lift * sc;                        /* a hop lifts the kart, not its shadow */
    const int x0 = (int)floorf(sx - KART_OX * k), x1 = (int)ceilf(sx + (KART_SW - KART_OX) * k);
    const int y0 = (int)floorf(sy - KART_OY * k - up), y1 = (int)ceilf(sy + (KART_SH - KART_OY) * k);
    const float inv = 1.0f / k;
    /* The sprite's box, clipped to the frame's left and top. */
    int px0 = x0, py0 = y0;
    if ( 0 > x0 )
    {
        px0 = 0;
    }
    if ( 0 > y0 )
    {
        py0 = 0;
    }
    for ( int py = py0; (y1 > py) && (RD_VIEW_H > py); py++ )
    {
        const float v_kart = ((float)py + 0.5f - (sy - up)) * inv + KART_OY;
        const float v_ground = ((float)py + 0.5f - sy) * inv + KART_OY;
        for ( int px = px0; (x1 > px) && (RD_W > px); px++ )
        {
            const int u = (int)(((float)px + 0.5f - sx) * inv + KART_OX);
            if ( KART_SW <= (unsigned)u )
            {
                continue;
            }
            const int vk = (int)v_kart, vg = (int)v_ground;
            if ( ghost && ((px + py) & 1) )
            {
                continue;   /* a ghost: every other pixel, no shadow */
            }
            uint16_t *dst = &fb[py * RD_W + px];
            uint8_t c = 0;
            if ( (KART_SH > (unsigned)vk) && (0.0f <= v_kart) )
            {
                c = spr[vk * KART_SW + u];
            }
            if ( c && (SPR_SHADOW != c) )
            {
                *dst = pal[c];
            }
            else if ( !ghost && (KART_SH > (unsigned)vg) && (0.0f <= v_ground) && (SPR_SHADOW == spr[vg * KART_SW + u]) )
            {
                *dst = darken(*dst);
            }
        }
    }
}

void rd_billboard(uint16_t *fb, const rd_camera_t *cam, float x, float y, float lift, rd_item_sprite_t what, float wide)
{
    float sx, sy, sc;
    if ( (RD_ITEM_SPRITES <= what) || !rd_project(cam, x, y, &sx, &sy, &sc) )
    {
        return;
    }
    const item_sprite_t *sp = ITEM_SPRITES[what];
    const float k = sc * wide / (float)sp->w;          /* screen pixels per sprite pixel */
    if ( 0.15f > k )
    {
        return;
    }
    const float top = sy - lift * sc - (float)sp->h * k, left = sx - 0.5f * (float)sp->w * k;
    const int x0 = (int)floorf(left), x1 = (int)ceilf(left + sp->w * k);
    const int y0 = (int)floorf(top), y1 = (int)ceilf(top + sp->h * k);
    const float inv = 1.0f / k;
    /* The sprite's box, clipped to the frame's left and top. */
    int px0 = x0, py0 = y0;
    if ( 0 > x0 )
    {
        px0 = 0;
    }
    if ( 0 > y0 )
    {
        py0 = 0;
    }
    for ( int py = py0; (y1 > py) && (RD_VIEW_H > py); py++ )
    {
        const int v = (int)(((float)py + 0.5f - top) * inv);
        if ( (unsigned)v >= sp->h )
        {
            continue;
        }
        for ( int px = px0; (x1 > px) && (RD_W > px); px++ )
        {
            const int u = (int)(((float)px + 0.5f - left) * inv);
            if ( (unsigned)u >= sp->w )
            {
                continue;
            }
            const uint8_t c = sp->px[v * sp->w + u];
            if ( c )
            {
                fb[py * RD_W + px] = s_items[c];
            }
        }
    }
}

void rd_icon_size(rd_item_sprite_t what, int *w, int *h)
{
    /* An unknown sprite measures as the item box. */
    rd_item_sprite_t known = RD_BOX;
    if ( RD_ITEM_SPRITES > what )
    {
        known = what;
    }
    const item_sprite_t *sp = ITEM_SPRITES[known];
    *w = sp->w;
    *h = sp->h;
}

void rd_icon(uint16_t *fb, int x, int y, rd_item_sprite_t what, int scale)
{
    if ( RD_ITEM_SPRITES <= what )
    {
        return;
    }
    const item_sprite_t *sp = ITEM_SPRITES[what];
    for ( int v = 0; sp->h * scale > v; v++ )
    {
        for ( int u = 0; sp->w * scale > u; u++ )
        {
            const int X = x + u, Y = y + v;
            const uint8_t c = sp->px[(v / scale) * sp->w + u / scale];
            if ( c && (0 <= X) && (0 <= Y) && (RD_W > X) && (RD_H > Y) )
            {
                fb[Y * RD_W + X] = s_items[c];
            }
        }
    }
}
