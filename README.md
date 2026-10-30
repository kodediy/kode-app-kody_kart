# Kody Kart

Kody - the same Kody as Kody Runner and Kodegotchi, in the round - races his
five cousins round three circuits on the Kode Dot. A kart racer in the style of
the 16-bit classics: a Mode 7 track seen from behind the kart, drifts with a
mini-turbo, item boxes, seeds for coins, a Grand Prix cup, time trials against
your own ghost. The cable stays alive while it runs (the SDK's console).

Design notes: `docs/architecture/decisions/2026-10-19-kody-kart-design.md` in kodediy/ecosystem, where the game was born.

## Playing

| | |
|---|---|
| **TOP** (held) | the throttle - the Dot's "A" |
| **LEFT / RIGHT** | steer |
| **UP** | hop; held while steering, a **drift** |
| **DOWN** | brake; held when stopped, reverse |
| **BOT** (tap) | use the item |
| **BOT** (hold 1 s) | home to kodeOS, from any screen |
| **touch** | pause (TOP races on, BOT gives up) |

In the menus: the pad chooses, TOP goes, BOT goes back.

- **Drift**: hop and steer, keep UP held. Sparks turn **blue**, then
  **orange**; let go for a mini-turbo (orange is longer and stronger).
  Steering into the drift tightens it, against it widens it.
- **Rocket start**: press the throttle just after the **2** of the countdown.
  From the **3**, the engine floods.
- **Seeds** on the track are the coins: +2 % top speed each, up to 10. A hit
  costs two.
- **Grass** caps you near half speed, **sand** lower. Hops, turbos and the
  golden seed ignore the ground.

## Items

From the **?** boxes (three rows per circuit), after a one-second roulette,
weighted by your place - the leader gets things to defend with, the back of
the pack things to catch up with:

| Item | Does |
|---|---|
| **Turbo** (a sunflower) | a strong boost |
| **Peel** | dropped behind you; whoever drives over it spins (hop over it) |
| **Acorn** | thrown forward, rolls along the track in its lane, spins the first kart it reaches |
| **Golden seed** | six seconds of star: faster, untouchable, bumps spin others |

The rivals use them too: a turbo on a straight, a peel when someone is right
behind, an acorn at whoever is ahead in their lane.

## Modes and circuits

- **Grand Prix**: the three circuits in turn, 9-6-4-3-2-1 points, the standings and
  a golden-seed trophy at the end.
- **Single race**: the menu's circuit.
- **Time trial**: alone, three turbos, your record's **ghost** racing you
  (saved per circuit, shown at the engine it was set at).
- **Engines**: 50cc, 100cc, 150cc - everyone's. Kody's engine is the rivals'
  average (they spread 96-104 % around it), with a rubber band both ways.

| Circuit | |
|---|---|
| **Kody Circuit** (park, anticlockwise) | the start straight, a long sweeper with sand outside, an S, a hairpin, the dip (a tight U with a sand shortcut across its bottom - worth it with a turbo), the top-left, a wavy run home. A lake in the infield. |
| **Kody Beach** (beach, clockwise) | a fast top straight, a long right-hander, a chicane, a hairpin by the sea, a dune wiggle with a sand cut across its foot right after a pad, a tight S back up. |
| **Kody Snow** (snow, anticlockwise) | up the right side, a long icy sweeper, a hairpin at the top, down the middle over the frozen stretch, a hairpin round the snowbank, the top-left, down the left side home. Ice has no grip: lift off before it, or hop across. |

Records (best race and best lap per circuit, mode and engine; races, wins,
podiums) and ghosts are saved with the SDK (`khal_nvs`, namespace `kodykart`).

## Build and install

ESP-IDF **v6.1** and an SSH key that can read `kodediy/kode-sdk` (the first
build fetches the SDK, pinned at `v00.00.27` in `main/idf_component.yml`).

```bash
export PATH="$HOME/.local/idf-python/bin:$PATH"
. ~/esp/esp-idf-v6.1/export.sh
idf.py build
kode install build/kody_kart.bin      # over the console on the cable; boots it
```

**Never `idf.py flash`**: it writes over kodeOS's bootloader and partition
table. Any reset returns to kodeOS.

## From the terminal

```bash
kode send 'variable name="summary"'              # race,3/6,2/3,138 = screen,place/racers,lap/laps,speed
kode send 'variable_list'                         # every variable and its words
kode send 'function name="race" arg=150cc'        # a single race, now (enum words go without quotes)
kode send 'function name="trial" arg=100cc'       # a time trial, now
kode send 'function name="circuit" arg=beach'     # the next one's circuit: park, beach or snow
python3 tools/screenshot.py shot.bmp              # what the glass shows (close `kode console` first)
```

## How it is built

The glass is driven without LVGL: the game paints a logical 205x251 frame,
doubles every pixel straight into the panel's back buffer and flips it
(`kdrv_display_back_buffer`) - ~45-50 fps, against 17 through an lv_canvas.

Everything that decides anything is pure C, compiled on the host by
`tools/host_tests.sh` (`host/test_*.c`, each prints `OK <n>`):

| File | |
|---|---|
| `track.c`, `circuits.c` | a circuit is a closed Catmull-Rom spline + features, rasterized at boot into a texture, a progress map (laps, places, the rivals' line, WRONG WAY, all without checkpoints) and the decor |
| `kart.c` | one kart: throttle, steering, the drift and its turbo, surfaces, walls |
| `race.c`, `ai.c`, `items.c` | the race (grid, countdown, laps, places, bumps, the rocket start), the rivals' brain, boxes, seeds and items |
| `render.c`, `hud.c`, `font.c`, `screens.c` | Mode 7 ground, sky, billboards and effects; the HUD and minimap; the menus |
| `game.c`, `records.c`, `ghost.c` | the screens' rules and the cup; records; the time trial's ghost |
| `kart_ao.c`, `sound.c`, `console.c` | the adapter: the frame loop, input, sound, haptic, LED, saves, the console |

Tools: `tools/gen-sprites.mjs` ray-casts the voxel kart with Kody at the
wheel from 16 sides and packs the hand-drawn item and decor sprites into
`main/sprites_gen.h` (and `tools/preview.png`). `host/track_preview.c` draws a
circuit from above with each control point's progress; `host/frame_preview.c`
draws any frame or screen as the Dot would show it.
