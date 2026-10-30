/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file track.h
 * @brief A circuit: a closed spline and its features, rasterized into a texture and a progress map.
 *
 * Pure C: no SDK. The caller hands the buffers (PSRAM on the Dot, malloc on
 * the host). A circuit is DATA (tk_def_t): control points of a closed
 * Catmull-Rom spline over a TK_SIZE x TK_SIZE world, the road's half width,
 * and spans along the centre line (sand on one side, boost pads, a sand
 * shortcut). track_build() walks the spline one texel at a time and stamps a
 * band across its normal, keeping per texel the nearest centre-line point:
 * how far from it (signed: left is negative) and how far round the lap it
 * is. Everything else - the look, the surface, laps, positions, the rivals'
 * line, the WRONG WAY sign - is read from those two maps.
 */
#ifndef TRACK_H_
#define TRACK_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TK_SIZE      1024          /**< the world is TK_SIZE x TK_SIZE texels */
#define TK_NO_PROG   0xFFFFu       /**< a texel no centre-line point reached  */
#define TK_MAX_LINE  6000          /**< longest centre line, in texels        */

/** @brief What a kart feels under its wheels. */
typedef enum {
    TK_ROAD = 0,
    TK_CURB,      /* road, a little bumpy                       */
    TK_GRASS,     /* slow                                       */
    TK_SAND,      /* slower                                     */
    TK_BOOST,     /* a turbo pad                                */
    TK_ICE,       /* road with no grip: the kart slides         */
    TK_WALL,      /* the tyre wall: solid                       */
    TK_OUT,       /* beyond the walls: solid                    */
    TK_SURF_COUNT
} tk_surface_t;

/** @brief Ground palette indices: what a texel holds. */
enum {
    TK_C_ASPHALT = 0, TK_C_ASPHALT2, TK_C_LINE, TK_C_CURB_R, TK_C_CURB_W,
    TK_C_GRASS, TK_C_GRASS2, TK_C_SAND, TK_C_SAND2, TK_C_WALL_R, TK_C_WALL_W,
    TK_C_FOREST, TK_C_FOREST2, TK_C_CHECK_B, TK_C_CHECK_W, TK_C_BOOST, TK_C_BOOST2,
    TK_C_GRID, TK_C_WATER, TK_C_WATER2,
    TK_C_SHORE,     /* just behind the wall: forest in the park, dry sand on the beach (solid either way) */
    TK_C_ICE, TK_C_ICE2,
    TK_C_COUNT
};

/** @brief A span along the centre line: [s0, s1) texels from the line, lateral [o0, o1]. */
typedef enum { TK_F_SAND = 0, TK_F_BOOST, TK_F_ITEMS, TK_F_SEEDS, TK_F_ICE } tk_feature_kind_t;

typedef struct {
    uint8_t  kind;      /* tk_feature_kind_t */
    uint16_t s0, s1;    /* along the centre line, texels from the start line */
    int8_t   o0, o1;    /* across it, texels; left of the direction of travel is negative */
} tk_feature_t;

typedef struct { int16_t x, y; } tk_pt_t;

/** @brief A circuit's look: the same surfaces, other colours and other things by the track. */
typedef enum { TK_THEME_PARK = 0, TK_THEME_BEACH, TK_THEME_SNOW, TK_THEMES } tk_theme_t;

/** @brief A circuit, as data. */
typedef struct {
    const char         *name;
    const tk_pt_t      *pts;        /* control points, in driving order; closed */
    uint8_t             n_pts;
    uint8_t             half_w;     /* road half width, texels */
    uint8_t             verge;      /* grass between the road and the wall, texels */
    const tk_feature_t *features;
    uint8_t             n_features;
    const tk_pt_t      *cut;        /* a sand shortcut: an open polyline, or NULL */
    uint8_t             n_cut;
    tk_pt_t             lake, lake_r; /* an ellipse of water off the band (centre, radii); r 0 = none */
    uint8_t             laps;
    uint8_t             theme;      /* tk_theme_t: how the ground and the decor look */
} tk_def_t;

/** @brief One sampled centre-line point, one texel apart. */
typedef struct {
    float x, y;
    float dx, dy;       /* unit direction of travel */
    float curv;         /* signed turn rate, radians per texel (+ = right) */
} tk_line_t;

#define TK_MAX_DECOR 200

/** @brief What stands by the track. */
typedef enum { TK_D_TREE = 0, TK_D_BUSH, TK_D_SIGN, TK_D_TYRES, TK_D_KINDS } tk_decor_kind_t;

typedef struct { int16_t x, y; uint8_t kind; } tk_decor_t;

/** @brief A built circuit. */
typedef struct {
    const tk_def_t *def;
    uint8_t  *tex;       /* TK_SIZE^2 ground palette indices (TK_C_*) */
    uint16_t *prog;      /* TK_SIZE^2 texels round the lap, or TK_NO_PROG */
    int8_t   *off;       /* TK_SIZE^2 signed distance to the centre line (clamped) */
    tk_line_t *line;     /* TK_MAX_LINE */
    int       length;    /* centre-line points = lap length in texels */
    tk_decor_t decor[TK_MAX_DECOR];   /* trees, signs... behind the walls */
    int       n_decor;
} track_t;

/**
 * @brief Rasterize @p def into @p t.
 *
 * The buffers must be set (tex, prog, off, line) and are overwritten.
 *
 * @param t   The track to fill, its buffers already allocated.
 * @param def The circuit to build.
 * @return false when the spline is longer than TK_MAX_LINE.
 */
bool track_build(track_t *t, const tk_def_t *def);

/**
 * @brief What is under (x, y). Outside the world is TK_OUT.
 * @param t A built track.
 * @param x World x, texels.
 * @param y World y, texels.
 * @return The surface at that point.
 */
tk_surface_t track_surface(const track_t *t, float x, float y);

/**
 * @brief The ground colour index at integer texel (x, y), TK_C_FOREST outside.
 * @param t A built track.
 * @param x Texel x.
 * @param y Texel y.
 * @return A TK_C_* index.
 */
static inline uint8_t track_texel(const track_t *t, int x, int y)
{
    if ( (TK_SIZE <= (unsigned)x) || (TK_SIZE <= (unsigned)y) )
    {
        return TK_C_FOREST;
    }
    return t->tex[y * TK_SIZE + x];
}

/**
 * @brief Progress at (x, y), TK_NO_PROG off the stamped band.
 * @param t A built track.
 * @param x World x, texels.
 * @param y World y, texels.
 * @return Texels round the lap, or TK_NO_PROG.
 */
uint16_t track_prog(const track_t *t, float x, float y);

/**
 * @brief Signed lateral offset at (x, y) (meaningful where prog is).
 * @param t A built track.
 * @param x World x, texels.
 * @param y World y, texels.
 * @return Texels from the centre line; left of travel is negative.
 */
int track_offset(const track_t *t, float x, float y);

/**
 * @brief The centre-line point @p s texels round the lap (wraps either way).
 * @param t A built track.
 * @param s Texels round the lap, any sign or size.
 * @return The centre-line point.
 */
const tk_line_t *track_at(const track_t *t, int s);

/**
 * @brief The surface a ground colour index stands for.
 * @param c A TK_C_* index.
 * @return The surface a kart feels there.
 */
tk_surface_t track_surface_of(uint8_t c);

/** @brief The circuits this app knows. */
extern const tk_def_t TK_KODY_CIRCUIT;
extern const tk_def_t TK_KODY_BEACH;
extern const tk_def_t TK_KODY_SNOW;

#define TK_CIRCUITS 3
extern const tk_def_t *const TK_ALL[TK_CIRCUITS];

#ifdef __cplusplus
}
#endif

#endif /* TRACK_H_ */
