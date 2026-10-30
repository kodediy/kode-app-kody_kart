/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_ghost.c
 * @brief Host checks for ghost.c. Prints "OK <n>".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ghost.h"

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
    static ghost_t g;
    ghost_start(&g, 1, 2);
    CHECK(0 == g.n && 1 == g.circuit && 2 == g.cc);
    float x, y, a;
    CHECK(!ghost_at(&g, 0.0f, &x, &y, &a));
    /* A kart going round a circle at 60 steps a second: 15 samples a second are kept. */
    for ( int s = 0; 600 >= s; s++ )
    {
        const float t = s / 60.0f, ang = t * 0.5f;
        ghost_add(&g, t, 500.0f + 100.0f * cosf(ang), 500.0f + 100.0f * sinf(ang), ang + 1.5708f);
    }
    CHECK(150 <= g.n && 151 >= g.n);
    /* Played back between samples: close to the truth. */
    for ( float t = 0.0f; 9.9f > t; t += 0.37f )
    {
        CHECK(ghost_at(&g, t, &x, &y, &a));
        const float ang = t * 0.5f;
        CHECK(fabsf(x - (500.0f + 100.0f * cosf(ang))) < 0.6f);
        CHECK(fabsf(y - (500.0f + 100.0f * sinf(ang))) < 0.6f);
        float da = a - (ang + 1.5708f);
        while ( 3.14159f < da ) da -= 6.28318f;
        while ( -3.14159f > da ) da += 6.28318f;
        CHECK(fabsf(da) < 0.05f);
    }
    CHECK(!ghost_at(&g, 30.0f, &x, &y, &a));   /* past its end */
    /* The heading wraps the short way. */
    ghost_start(&g, 0, 0);
    ghost_add(&g, 0.0f, 0, 0, 6.2f);
    ghost_add(&g, 1.0f / GHOST_HZ, 0, 0, 0.1f);
    CHECK(ghost_at(&g, 0.5f / GHOST_HZ, &x, &y, &a));
    CHECK(fabsf(a - 6.2832f) < 0.1f || fabsf(a) < 0.1f);
    /* Full: nothing more is kept. */
    ghost_start(&g, 0, 0);
    for ( int s = 0; GHOST_MAX + 50 > s; s++ ) ghost_add(&g, (float)s / GHOST_HZ, 1, 1, 0);
    CHECK(GHOST_MAX == g.n);
    printf("OK %d\n", n_ok);
    return 0;
}
