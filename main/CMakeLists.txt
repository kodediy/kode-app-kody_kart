# kode is the facade (kode_init, the buttons, the LED, the haptic, the tone,
# going home).
# The race itself is pure C and needs none of them.
idf_component_register(
    SRCS "app_main.c" "kart_ao.c" "track.c" "circuits.c" "palette.c" "render.c" "kart.c" "race.c" "ai.c" "items.c" "font.c" "hud.c" "sound.c" "game.c" "screens.c" "records.c" "records_save.c" "console.c" "ghost.c" "ghost_save.c"
    INCLUDE_DIRS "."
    REQUIRES kode
    PRIV_REQUIRES kdrv khal esp_hw_support esp_timer
)
