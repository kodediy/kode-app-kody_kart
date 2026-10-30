/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file circuits.c
 * @brief The circuits, as data (track.h tk_def_t).
 */
#include "track.h"

/* Kody Circuit, driven anticlockwise on the map: the start straight along the
 * bottom heading east, up the right side, west along the top with a dip into
 * the infield, and down the left side back to the line. */
static const tk_pt_t KODY_PTS[] = {
    {300, 880}, {560, 880}, {760, 875},             /* the start straight            */
    {900, 820}, {930, 690},                          /* T1: a long left sweeper       */
    {880, 580}, {920, 470}, {900, 340},              /* the S up the right side       */
    {850, 230}, {760, 180}, {650, 200},              /* T3: the top-right hairpin     */
    {580, 300}, {560, 430}, {480, 500}, {420, 430},  /* the dip: down, round, up      */
    {420, 300}, {360, 180}, {240, 140},              /* out of the dip, the top-left  */
    {130, 230}, {110, 380}, {160, 520}, {120, 660},  /* down the left side            */
    {150, 800}, {220, 870},                          /* the last bend onto the line   */
};

/* Where the fun is (s = texels from the line, see host/track_preview.c):
 * sand outside the two fast left-handers punishes a wide line; a pad on the
 * straight rewards the inside; the pad out of the dip rewards a clean
 * hairpin; the shortcut over the dip's sand only pays with a turbo. */
static const tk_feature_t KODY_FEATURES[] = {
    { TK_F_SAND,  600,  790,  22,  36 },  /* outside T1                 */
    { TK_F_SAND, 2280, 2470,  22,  36 },  /* outside the top-left       */
    { TK_F_SAND, 1790, 1880, -36, -22 },  /* outside the dip's hairpin  */
    { TK_F_BOOST, 330,  356, -18,  -4 },  /* the straight, on the left  */
    { TK_F_BOOST,1985, 2010,  -8,   8 },  /* out of the dip             */
    { TK_F_BOOST,2860, 2885,   4,  18 },  /* the left side's kink       */
    { TK_F_ITEMS, 180,  181, -16,  16 },  /* item boxes: the straight   */
    { TK_F_ITEMS,1530, 1531, -16,  16 },  /*   the top, before the dip  */
    { TK_F_ITEMS,2700, 2701, -16,  16 },  /*   down the left side       */
    { TK_F_SEEDS, 470,  550,  12,  12 },  /* seed rows: a lane each     */
    { TK_F_SEEDS,1020, 1100, -10, -10 },
    { TK_F_SEEDS,2090, 2170,   0,   0 },
    { TK_F_SEEDS,2560, 2640,   8,   8 },
    { TK_F_SEEDS,3000, 3080,  -8,  -8 },
};

/* Straight across the bottom of the dip, through both walls. */
static const tk_pt_t KODY_CUT[] = { {556, 405}, {490, 412}, {426, 405} };

const tk_def_t TK_KODY_CIRCUIT = {
    .name = "KODY CIRCUIT",
    .pts = KODY_PTS,
    .n_pts = sizeof KODY_PTS / sizeof KODY_PTS[0],
    .half_w = 22,
    .verge = 14,
    .features = KODY_FEATURES,
    .n_features = sizeof KODY_FEATURES / sizeof KODY_FEATURES[0],
    .cut = KODY_CUT,
    .n_cut = sizeof KODY_CUT / sizeof KODY_CUT[0],
    .lake = {520, 700},
    .lake_r = {250, 85},
    .laps = 3,
};

/* Kody Beach, driven clockwise: a fast run along the top, a long right-hander
 * down to a chicane, a hairpin by the sea, a twisty climb back through the
 * dunes. The verge is sand all round; outside the walls, the sea. */
static const tk_pt_t BEACH_PTS[] = {
    {220, 190}, {480, 170}, {760, 190},              /* the start straight        */
    {890, 290}, {900, 440},                          /* the long right-hander     */
    {770, 520}, {860, 620},                          /* the chicane               */
    {900, 780}, {820, 890}, {690, 870},              /* the hairpin by the sea    */
    {620, 760}, {500, 720}, {420, 820},              /* the dune wiggle           */
    {290, 870}, {160, 800},                          /* the bottom-left bend      */
    {130, 640}, {265, 530}, {150, 410},              /* the climb: an S           */
    {130, 280},                                      /* the last bend onto the line */
};

static const tk_feature_t BEACH_FEATURES[] = {
    { TK_F_BOOST, 240,  266,  -6,   8 },   /* the straight                */
    { TK_F_BOOST,1700, 1725,  -8,   8 },   /* out of the dune wiggle      */
    { TK_F_ITEMS, 150,  151, -16,  16 },
    { TK_F_ITEMS,1080, 1081, -16,  16 },
    { TK_F_ITEMS,2250, 2251, -16,  16 },
    { TK_F_SEEDS, 400,  480,  -8,  -8 },
    { TK_F_SEEDS, 900,  980,   8,   8 },
    { TK_F_SEEDS,1500, 1580,   0,   0 },
    { TK_F_SEEDS,2500, 2580, -10, -10 },
};

/* Across the foot of the dune: deep sand, worth it with a turbo. */
static const tk_pt_t BEACH_CUT[] = { {616, 772}, {540, 790}, {458, 784} };

const tk_def_t TK_KODY_BEACH = {
    .name = "KODY BEACH",
    .pts = BEACH_PTS,
    .n_pts = sizeof BEACH_PTS / sizeof BEACH_PTS[0],
    .half_w = 21,
    .verge = 16,
    .features = BEACH_FEATURES,
    .n_features = sizeof BEACH_FEATURES / sizeof BEACH_FEATURES[0],
    .cut = BEACH_CUT,
    .n_cut = sizeof BEACH_CUT / sizeof BEACH_CUT[0],
    .laps = 3,
    .theme = TK_THEME_BEACH,
};

/* Kody Snow, anticlockwise: out of the line up the right side, a long icy
 * sweeper, a hairpin at the top, down through the middle over the frozen
 * stretch, a hairpin round the snowbank, up and round the top-left, and down
 * the left side home. Ice is grip-less: lift off before it, or hop across. */
static const tk_pt_t SNOW_PTS[] = {
    {500, 860}, {760, 860},                          /* the start straight         */
    {900, 780}, {920, 620},                          /* up the right side          */
    {860, 470}, {900, 330},                          /* the icy sweeper            */
    {840, 190}, {700, 150}, {620, 230},              /* the top hairpin            */
    {640, 380}, {560, 500},                          /* down the middle: the ice   */
    {420, 520}, {330, 430},                          /* round the snowbank         */
    {300, 280}, {200, 160}, {110, 260},              /* up and round the top-left  */
    {130, 420}, {220, 560}, {140, 700},              /* down the left side         */
    {220, 840}, {360, 880},                          /* home                       */
};

static const tk_feature_t SNOW_FEATURES[] = {
    { TK_F_ICE,    820, 1000, -21,  21 },  /* the icy sweeper             */
    { TK_F_ICE,   1500, 1640, -21,  21 },  /* the frozen descent          */
    { TK_F_BOOST,  300,  326,  -8,   8 },
    { TK_F_BOOST, 2600, 2626,   4,  18 },
    { TK_F_SAND,  1180, 1330,  22,  38 },  /* deep snow outside the top hairpin */
    { TK_F_ITEMS,  150,  151, -16,  16 },
    { TK_F_ITEMS, 1100, 1101, -16,  16 },
    { TK_F_ITEMS, 2200, 2201, -16,  16 },
    { TK_F_SEEDS,  420,  500,   8,   8 },
    { TK_F_SEEDS, 1700, 1780,   0,   0 },
    { TK_F_SEEDS, 2400, 2480,  -8,  -8 },
};

const tk_def_t TK_KODY_SNOW = {
    .name = "KODY SNOW",
    .pts = SNOW_PTS,
    .n_pts = sizeof SNOW_PTS / sizeof SNOW_PTS[0],
    .half_w = 21,
    .verge = 15,
    .features = SNOW_FEATURES,
    .n_features = sizeof SNOW_FEATURES / sizeof SNOW_FEATURES[0],
    .laps = 3,
    .theme = TK_THEME_SNOW,
    .lake = {520, 700},
    .lake_r = {150, 70},
};

const tk_def_t *const TK_ALL[TK_CIRCUITS] = { &TK_KODY_CIRCUIT, &TK_KODY_BEACH, &TK_KODY_SNOW };
