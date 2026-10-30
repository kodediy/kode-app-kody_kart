/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file ghost_save.c
 * @brief A circuit's ghost on the Dot: one blob per circuit in khal_nvs.
 *
 * Only the samples it kept are saved (~7 KB for a race).
 */
#include "ghost.h"

#include <stdio.h>

#include "khal_nvs.h"

#define NS "kodykart"

static void key_for(char *key, size_t cap, int circuit) { snprintf(key, cap, "ghost%d", circuit); }

bool ghost_load(ghost_t *g, int circuit)
{
    char key[12];
    key_for(key, sizeof key, circuit);
    size_t len = sizeof *g;
    if ( (KODE_OK != khal_nvs_get_blob(NS, key, g, &len)) || (GHOST_VERSION != g->version) || (GHOST_MAX < g->n) ||
         (GHOST_BYTES(g) != len) || (circuit != g->circuit) )
    {
        g->n = 0;
        g->time_cs = 0;
        return false;
    }
    return true;
}

bool ghost_save(const ghost_t *g)
{
    char key[12];
    key_for(key, sizeof key, g->circuit);
    return KODE_OK == khal_nvs_set_blob(NS, key, g, GHOST_BYTES(g));
}
