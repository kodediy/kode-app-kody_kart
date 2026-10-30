/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file palette.c
 * @brief The ground's colours: a sunny circuit, the brand's red and yellow on it.
 */
#include "palette.h"

const uint32_t PAL_GROUND[TK_C_COUNT] = {
    [TK_C_ASPHALT]  = 0x8A8A92,
    [TK_C_ASPHALT2] = 0x7E7E86,
    [TK_C_LINE]     = 0xF2F2F2,
    [TK_C_CURB_R]   = PAL_RED,
    [TK_C_CURB_W]   = 0xF2F2F2,
    [TK_C_GRASS]    = 0x58B040,
    [TK_C_GRASS2]   = 0x4CA238,
    [TK_C_SAND]     = 0xEAD08A,
    [TK_C_SAND2]    = 0xD6B66A,
    [TK_C_WALL_R]   = PAL_RED,
    [TK_C_WALL_W]   = PAL_WHITE,
    [TK_C_FOREST]   = 0x2F7D33,
    [TK_C_FOREST2]  = 0x24692A,
    [TK_C_CHECK_B]  = PAL_INK,
    [TK_C_CHECK_W]  = PAL_WHITE,
    [TK_C_BOOST]    = PAL_YELLOW,
    [TK_C_BOOST2]   = 0xFF6A00,
    [TK_C_GRID]     = PAL_WHITE,
    [TK_C_WATER]    = 0x2E86C8,
    [TK_C_WATER2]   = 0x8CC8F0,
    [TK_C_SHORE]    = 0x2F7D33,   /* the park's forest */
    [TK_C_ICE]      = 0xBFE6F6,
    [TK_C_ICE2]     = 0xE8F8FF,
};

uint32_t pal_ground(int theme, int c)
{
    if ( TK_THEME_BEACH == theme )
    {
        switch ( c )
        {
        case TK_C_GRASS:   return 0xF4E4B8;   /* pale beach sand on the verge */
        case TK_C_GRASS2:  return 0xECD8A4;
        case TK_C_FOREST:  return 0x2E9AD2;   /* the sea beyond the walls */
        case TK_C_FOREST2: return 0x7CC8EC;
        case TK_C_CURB_R:  return PAL_KODY_BLUE;
        case TK_C_SHORE:   return 0xE6CF92;   /* dry sand behind the wall */
        default: break;
        }
    }
    if ( TK_THEME_SNOW == theme )
    {
        switch ( c )
        {
        case TK_C_GRASS:   return 0xF4F8FC;   /* snow on the verge */
        case TK_C_GRASS2:  return 0xE4ECF4;
        case TK_C_SAND:    return 0xDCE6F0;   /* deep snow */
        case TK_C_SAND2:   return 0xC8D6E4;
        case TK_C_FOREST:  return 0xE8F0F6;   /* snowfields */
        case TK_C_FOREST2: return 0xD2DEEA;
        case TK_C_SHORE:   return 0xF0F5FA;
        case TK_C_WATER:   return 0xA8D4EC;   /* the lake, frozen */
        case TK_C_WATER2:  return 0xE0F2FC;
        case TK_C_ASPHALT: return 0x7C8088;   /* a colder, darker road */
        case TK_C_ASPHALT2:return 0x70747C;
        default: break;
        }
    }
    return PAL_GROUND[c];
}
