#!/bin/bash
# The host checks: every host/test_*.c against the pure C it tests, plus a few frames from
# host/frame_preview.c as BMPs. Usage: tools/host_tests.sh [outdir]
set -u
A=$(cd "$(dirname "$0")/.." && pwd); O=${1:-/tmp/kody_kart_host}; mkdir -p "$O"; cd "$A"
srcs_of() { case $1 in
  test_track) echo "track.c circuits.c";;
  test_kart) echo "track.c circuits.c kart.c";;
  test_race|test_items) echo "track.c circuits.c kart.c race.c ai.c items.c";;
  test_render) echo "track.c palette.c circuits.c render.c race.c ai.c items.c kart.c hud.c font.c game.c screens.c records.c";;
  test_hud) echo "font.c hud.c race.c ai.c items.c kart.c track.c render.c palette.c";;
  test_game) echo "game.c";; test_ghost) echo "ghost.c";; test_records) echo "records.c";;
  fp) echo "track.c circuits.c palette.c render.c kart.c race.c ai.c items.c font.c hud.c game.c screens.c records.c ghost.c";;
esac; }
fail=0
for t in test_track test_kart test_race test_items test_render test_hud test_game test_ghost test_records; do
  srcs=""; for s in $(srcs_of $t); do srcs="$srcs main/$s"; done
  if ! cc -std=gnu11 -O1 -Imain host/$t.c $srcs -lm -o $O/$t 2>$O/$t.err; then echo "BUILD FAIL $t"; head -5 $O/$t.err; fail=1; continue; fi
  out=$($O/$t | tail -1); echo "$t $out"; [[ $out == OK* ]] || fail=1
done
srcs=""; for s in $(srcs_of fp); do srcs="$srcs main/$s"; done
cc -std=gnu11 -O1 -Imain host/frame_preview.c $srcs -lm -o $O/fp 2>$O/fp.err || { echo "BUILD FAIL fp"; head -5 $O/fp.err; fail=1; }
for c in 0 1 2; do for s in 800 3200; do KK_CIRCUIT=$c $O/fp $O/race-$c-$s.bmp $s >/dev/null 2>&1; done; done
for sc in title menu pause results cup; do $O/fp $O/scr-$sc.bmp 3200 17 110 22 $sc >/dev/null 2>&1; done
echo "frames: $(ls $O/*.bmp 2>/dev/null | wc -l)"
exit $fail
