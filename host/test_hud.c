/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_hud.c
 * @brief Host checks for font.c + hud.c. Prints "OK <n>".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "hud.h"
#include "render.h"

static int n_ok;
#define CHECK(c)                                                  \
    do                                                            \
    {                                                             \
        if ( !(c) )                                               \
        {                                                         \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);    \
            exit(1);                                              \
        }                                                         \
        n_ok++;                                                   \
    } while ( 0 )

int main(void)
{
    char b[16];
    hud_time(b, sizeof b, 0.0f);    CHECK(!strcmp(b, "0:00.00"));
    hud_time(b, sizeof b, 83.456f); CHECK(!strcmp(b, "1:23.46"));
    hud_time(b, sizeof b, -2.0f);   CHECK(!strcmp(b, "0:00.00"));
    hud_time(b, sizeof b, 9999.0f); CHECK(!strcmp(b, "9:59.99"));
    CHECK(font_width("", 3) == 0);
    CHECK(font_width("A", 1) == 5);
    CHECK(font_width("LAP", 2) == (3 * 6 - 1) * 2);

    /* Text half off every edge writes nothing outside the frame (the sanitizer watches). */
    static uint16_t fb[RD_W * RD_H + 64];
    for ( int i = 0; 64 > i; i++ ) fb[RD_W * RD_H + i] = 0xBEEF;
    font_draw(fb, RD_W, RD_H, -20, -10, "WRONG WAY", 4, 0xFFFF, 0);
    font_draw(fb, RD_W, RD_H, RD_W - 10, RD_H - 5, "FINISH!", 4, 0xFFFF, 0);
    for ( int i = 0; 64 > i; i++ ) CHECK(0xBEEF == fb[RD_W * RD_H + i]);
    /* A lit pixel and its outline. */
    memset(fb, 0, sizeof fb);
    font_draw(fb, RD_W, RD_H, 10, 10, "1", 1, 0xFFFF, 0x1111);
    CHECK(0xFFFF == fb[10 * RD_W + 12]);   /* the 1's top */
    CHECK(0x1111 == fb[9 * RD_W + 12]);    /* above it, the outline */
    printf("OK %d\n", n_ok);
    return 0;
}
