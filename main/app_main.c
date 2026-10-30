/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file app_main.c
 * @brief Kody Kart: Kody, the Kode mascot, at the wheel of a kart.
 *
 * A kodeOS app on the kode SDK, in the shape of kode-app-template: kode_init()
 * brings kodeOS up (the panel, the buttons, the console on the cable) and one
 * Active Object owns the race and the glass (kart_ao.c).
 *
 * Any reset returns to kodeOS (CONFIG_KODEOS_BOOT_ROLE_APP).
 */
#include "kode_ao.h"

#include "console.h"
#include "kart_ao.h"

/* What the launcher shows on the pill, and which category it files the app under. */
KODE_APP_MANIFEST("Kody Kart", "games");

void app_main(void)
{
    kode_init();
    KartAo_ctor();
    KartAo_start();
    console_start();   /* `kode send 'variable name="summary"'` - the race, from the terminal */
}
