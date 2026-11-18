#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Copyright (c) 2026 DFD
#
# Runs one benchmark scene on a device and pulls its performance log.
#
#   tools/perf/run_scene.sh <adb serial> <scene> <out dir> [screenshot times, e.g. "30 60 90"]
#
# Scenes live in tools/perf/scenes/<scene>.txt: a header with "# title:", "# title_id:" and
# "# duration:", then the input script (see LattePerfLog.h). Before each run the game's save is
# deleted (only the emulated save inside the app's own folder), so every run starts the same way;
# it is pulled to ~/AndroidStudioProjects/Fractal-trabalho/save_backups/<title id>/<time> first (SAVE_BACKUPS overrides).
# KEEP_SAVE=1 leaves it in place.
# The "Record performance log" setting must be on and controller 1 must be a Wii U GamePad.
# PKG selects the app (default dev.dfd.fractal). PPC_DUMP=1 on a rooted device writes the emulated thread list to log.txt
# before the app is closed (see the end of the script), for runs that freeze.
set -u
exec </dev/null
DEV=$1; SCENE=$2; OUT=$3; SHOTS=${4:-}
PKG=${PKG:-dev.dfd.fractal}
HERE=$(cd "$(dirname "$0")" && pwd)
FILE="$HERE/scenes/$SCENE.txt"
[ -f "$FILE" ] || { echo "no scene $FILE"; exit 1; }
TITLE=$(sed -n 's/^# title: *//p' "$FILE")
TID=$(sed -n 's/^# title_id: *//p' "$FILE")
DUR=$(sed -n 's/^# duration: *//p' "$FILE")
FILES=/sdcard/Android/data/$PKG/files
A="adb -s $DEV"
mkdir -p "$OUT"

$A shell am force-stop $PKG
# without an emulated GamePad the input script has nothing to press and the scene never starts
$A shell cat "$FILES/controllerProfiles/controller0.xml" 2>/dev/null | grep -q 'Wii U GamePad' \
  || { echo "$SCENE: controller 1 is not a Wii U GamePad (controllerProfiles/controller0.xml)"; exit 1; }
# every save that is about to be removed or replaced is copied to the PC first (it may be the player's own progress)
backup_save() {
  local dir="$FILES/mlc01/usr/save/${TID:0:8}/${TID:8:8}"
  $A shell ls "$dir" >/dev/null 2>&1 || return 0
  local dst="${SAVE_BACKUPS:-$HOME/AndroidStudioProjects/Fractal-trabalho/save_backups}/$TID/$(date +%Y%m%d-%H%M%S)"
  mkdir -p "$dst"
  $A pull "$dir" "$dst" >/dev/null 2>&1 || { echo "$SCENE: could not back up the save, not deleting it"; exit 1; }
}
# KEEP_SAVE=1 keeps the title's save (scenes that need menus past the first boot)
if [ "${KEEP_SAVE:-0}" != 1 ]; then
  backup_save
  $A shell rm -rf "$FILES/mlc01/usr/save/${TID:0:8}/${TID:8:8}"
fi
# "# save: <name>" starts from a saved game: the folder SAVES/<name> (default ~/AndroidStudioProjects/Fractal-trabalho/saves, a copy of
# mlc01/usr/save/<high>/<low> made after playing to that point) replaces the title's save before each run
SAVE_NAME=$(sed -n 's/^# save: *//p' "$FILE")
if [ -n "$SAVE_NAME" ]; then
  SRC="${SAVES:-$HOME/AndroidStudioProjects/Fractal-trabalho/saves}/$SAVE_NAME"
  [ -d "$SRC" ] || { echo "$SCENE: save $SRC not found"; exit 1; }
  [ "${KEEP_SAVE:-0}" = 1 ] && backup_save
  $A shell rm -rf "$FILES/mlc01/usr/save/${TID:0:8}/${TID:8:8}"
  $A shell mkdir -p "$FILES/mlc01/usr/save/${TID:0:8}"
  $A push "$SRC" "$FILES/mlc01/usr/save/${TID:0:8}/${TID:8:8}" >/dev/null || { echo "$SCENE: could not restore save $SAVE_NAME"; exit 1; }
fi
$A shell mkdir -p "$FILES/perf"
$A push "$FILE" "$FILES/perf/input_$TID.txt" >/dev/null
before=$($A shell ls "$FILES/perf" | tr -d '\r' | grep "_$TID\$" | sort | tail -1)

# open the library and tap the game
# am start, not monkey: monkey sometimes never returns on the RG Cube
ACT=$($A shell cmd package resolve-activity --brief -c android.intent.category.LAUNCHER $PKG | tail -1 | tr -d '\r')
$A shell am start -n "$ACT" >/dev/null 2>&1
sleep 6
# never tap on another app: when the emulator is force-stopped, Android brings back whatever task was below it
in_front() { $A shell dumpsys window | grep -m1 mCurrentFocus | grep -q "$PKG"; }
in_front || { $A shell am start -n "$ACT" >/dev/null 2>&1; sleep 6; }
in_front || { echo "$SCENE: another app is in front ($($A shell dumpsys window | grep -m1 mCurrentFocus | tr -d '\r')), not tapping"; exit 1; }
# LAUNCH_PATH=<path from title_list_cache.xml>: open the game directly (a title the library does not list, seen on the RP5)
if [ -n "${LAUNCH_PATH:-}" ]; then
  $A shell am start -n $PKG/.emulation.EmulationActivity --es $PKG.LaunchPath "'$LAUNCH_PATH'" >/dev/null
  sleep 4
else
# the game may be below the visible rows: scroll down a few times until it shows up
for try in 1 2 3 4 5; do
  ui=$($A exec-out uiautomator dump /dev/tty 2>/dev/null)
  read -r x1 y1 x2 y2 < <(echo "$ui" | grep -o "text=\"$TITLE\"[^>]*bounds=\"[^\"]*\"" | head -1 | grep -o 'bounds=.*' | grep -o '[0-9]\+' | tr '\n' ' ')
  # narrow screens (Mi A3 in portrait) show the short name of combo titles: "New SUPER MARIO BROS. U" for "... + New SUPER LUIGI U"
  [ -z "${y2:-}" ] && [ "${TITLE%% +*}" != "$TITLE" ] && read -r x1 y1 x2 y2 < <(echo "$ui" | grep -o "text=\"${TITLE%% +*}\"[^>]*bounds=\"[^\"]*\"" | head -1 | grep -o 'bounds=.*' | grep -o '[0-9]\+' | tr '\n' ' ')
  # screen size in the current orientation = bounds of the root node ("wm size" is the natural orientation)
  read -r _ _ W H < <(echo "$ui" | grep -o 'bounds="[^"]*"' | head -1 | grep -o '[0-9]\+' | tr '\n' ' ')
  [ -n "${y2:-}" ] && break
  $A shell input swipe $((W / 2)) $((H * 8 / 10)) $((W / 2)) $((H * 4 / 10)) 800; sleep 3
done
[ -n "${y2:-}" ] || { echo "$TITLE not in the library"; exit 1; }
# a title at the end of the list can sit under the button legend: tap the cover above it instead
ty=$(( (y1 + y2) / 2 ))
[ $ty -gt $(( H * 85 / 100 )) ] && ty=$(( y1 - 2 * (y2 - y1) ))
$A shell input tap $(( (x1 + x2) / 2 )) $ty
# a tap that lands while the library is still settling only moves the focus (seen on the RP5): tap again until the game opens
for try in 1 2 3; do
  sleep 4
  $A shell dumpsys window | grep -m1 mCurrentFocus | grep -q EmulationActivity && break
  echo "$SCENE: the game did not open, tapping again"
  in_front || { echo "$SCENE: another app came to the front, not tapping"; break; }
  $A shell input tap $(( (x1 + x2) / 2 )) $ty
done
fi
start=$(date +%s)

# clocks and thermal state every 2 s (the RP5 caps its GPU at 400 of 905 MHz when hot, which changes every result)
$A shell "i=0; g=/sys/class/kgsl/kgsl-3d0; m=\$(ls -d /sys/class/devfreq/*.gpu 2>/dev/null | head -1); \
  c=\$(ls -d /sys/devices/system/cpu/cpufreq/policy* | tail -1); echo t_s,gpu_mhz,gpu_max_mhz,big_cpu_max_mhz,battery_c,skin_c,battery_ma,battery_mv; b=/sys/class/power_supply/battery; \
  s=/dev/null; for z in /sys/class/thermal/thermal_zone*; do case \$(cat \$z/type) in skin-therm-usr|board-thmzone) s=\$z/temp;; esac; done; \
  while [ \$i -lt $DUR ]; do \
    if [ -e \$g/gpuclk ]; then gc=\$(cat \$g/gpuclk); gm=\$(cat \$g/max_gpuclk); else gc=\$(cat \$m/cur_freq); gm=\$(cat \$m/max_freq); fi; \
    echo \$i,\$((gc/1000000)),\$((gm/1000000)),\$((\$(cat \$c/scaling_max_freq)/1000)),\$((\$(cat /sys/class/power_supply/battery/temp)/10)),\$((\$(cat \$s 2>/dev/null || echo 0)/1000)),\$((\$(cat \$b/current_now 2>/dev/null || echo 0)/1000)),\$((\$(cat \$b/voltage_now 2>/dev/null || echo 0)/1000)); \
    sleep 2; i=\$((i+2)); done" > "$OUT/clocks_$SCENE.csv" 2>/dev/null &

for t in $SHOTS; do
  now=$(( $(date +%s) - start )); [ $t -gt $now ] && sleep $(( t - now ))
  $A exec-out screencap -p > "$OUT/${SCENE}_${t}s.png"
done
now=$(( $(date +%s) - start )); [ $DUR -gt $now ] && sleep $(( DUR - now ))

alive=$($A shell pidof $PKG | tr -d '\r')
# PPC_DUMP=1 (rooted device): SIGSEGV the still running app first, so the crash handler writes the emulated thread list
# (which OS call each guest thread waits in) to log.txt. Meant for freezes, where no crash report would exist otherwise
if [ -n "${PPC_DUMP:-}" ]; then
  emu=$($A shell pidof $PKG:EmulationProcess | tr -d '\r')
  [ -n "$emu" ] && $A shell "su -c 'kill -SEGV $emu'" && sleep 4
fi
$A shell am force-stop $PKG
session=$($A shell ls "$FILES/perf" | tr -d '\r' | grep "_$TID\$" | sort | tail -1)
if [ -z "$session" ] || [ "$session" = "$before" ]; then echo "$SCENE: no new session (app alive: ${alive:-no})"; exit 1; fi
$A pull "$FILES/perf/$session" "$OUT/" >/dev/null
$A shell rm -f "$FILES/perf/input_$TID.txt"
echo "$SCENE: $OUT/$session (app alive at the end: ${alive:+yes})"
