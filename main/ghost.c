/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ghost.c
 * @brief Recording and playing back a ghost.
 */
#include "ghost.h"

#include <math.h>

void ghost_start(ghost_t *g, int circuit, int cc)
{
    g->version = GHOST_VERSION;
    g->circuit = (uint8_t)circuit;
    g->cc = (uint8_t)cc;
    g->n = 0;
    g->time_cs = 0;
}

void ghost_add(ghost_t *g, float t, float x, float y, float angle)
{
    if ( (0.0f > t) || (GHOST_MAX <= g->n) ) return;
    if ( (float)g->n / GHOST_HZ > t ) return;   /* not due yet */
    ghost_sample_t *s = &g->s[g->n];
    s->x = (int16_t)lrintf(x * 8.0f);
    s->y = (int16_t)lrintf(y * 8.0f);
    float turns = angle / (2.0f * (float)M_PI);
    turns -= floorf(turns);
    s->a = (uint8_t)((int)lrintf(turns * 256.0f) & 255);
    g->n++;
}

bool ghost_at(const ghost_t *g, float t, float *x, float *y, float *angle)
{
    if ( (2 > g->n) || (0.0f > t) ) return false;
    const float f = t * GHOST_HZ;
    const int i = (int)f;
    if ( i + 1 >= g->n ) return false;
    const float u = f - (float)i;
    const ghost_sample_t *p = &g->s[i], *q = &g->s[i + 1];
    *x = ((float)p->x + ((float)q->x - (float)p->x) * u) / 8.0f;
    *y = ((float)p->y + ((float)q->y - (float)p->y) * u) / 8.0f;
    /* The heading the short way round. */
    int da = (int)q->a - (int)p->a;
    if ( 128 < da ) da -= 256;
    if ( -128 > da ) da += 256;
    *angle = ((float)p->a + (float)da * u) * (2.0f * (float)M_PI / 256.0f);
    return true;
}
