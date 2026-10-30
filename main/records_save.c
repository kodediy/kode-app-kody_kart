/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file records_save.c
 * @brief The records on the Dot: one blob in khal_nvs, namespace "kodykart".
 */
#include "records.h"

#include "khal_nvs.h"

#define NS  "kodykart"
#define KEY "records"

void records_load(records_t *r)
{
    static records_t saved;   /* static: bigger than an AO's stack wants */
    size_t len = sizeof saved;
    if ( KODE_OK == khal_nvs_get_blob(NS, KEY, &saved, &len) )
    {
        if ( (sizeof saved == len) && (RECORDS_VERSION == saved.version) )
        {
            *r = saved;
            return;
        }
        /* An older record: carried over, saved anew at the next race. */
        const records_v1_t *v1 = (const records_v1_t *)&saved;
        if ( (sizeof *v1 == len) && (1 == v1->version) )
        {
            records_from_v1(r, v1);
            return;
        }
    }
    records_init(r);
}

bool records_save(const records_t *r)
{
    return KODE_OK == khal_nvs_set_blob(NS, KEY, r, sizeof *r);
}
