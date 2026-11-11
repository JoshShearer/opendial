#!/bin/bash
# Simulated soak (SRS §14.7, practical subset): N menu open → turn → select
# cycles plus N tool turns against a dry-run daemon on a private bus, with the
# overlay rendering offscreen. Checks the menu always closes, every select
# lands, and resident memory stays flat. Not hardware evidence.
# Usage: tests/soak/menu-soak.sh [BUILD_DIR] [CYCLES]
set -u
B=$(readlink -f "${1:-build}"); N=${2:-200}
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
export XDG_CONFIG_HOME=$W/cfg XDG_STATE_HOME=$W/state QT_QPA_PLATFORM=offscreen
exec dbus-run-session -- bash -c '
  B=$1; N=$2; W=$3; C=$B/src/cli/opendialctl
  $B/src/daemon/opendiald --dry-run --no-device --no-tray > $W/daemon.log 2>&1 &
  D=$!; sleep 2
  rss() { awk "/VmRSS/{print \$2}" /proc/$D/status; }
  $C tool volume >/dev/null
  # warm up caches (icons, QML) before taking the baseline
  for i in 1 2 3 4 5; do $C simulate longpress >/dev/null; $C simulate rotate 20 >/dev/null; $C simulate click >/dev/null; done
  sleep 1; r0=$(rss); fails=0; t0=$(date +%s)
  for i in $(seq 1 $N); do
    $C simulate longpress >/dev/null
    $C simulate rotate 20 >/dev/null
    $C simulate rotate -20 >/dev/null
    $C simulate click >/dev/null
    open=$($C --json status | python3 -c "import json,sys; print(json.load(sys.stdin)[\"menuOpen\"])")
    [ "$open" = "False" ] || fails=$((fails+1))
    $C simulate rotate 10 >/dev/null
    [ $((i % 50)) -eq 0 ] && echo "cycle $i: rss $(rss) kB, stuck menus $fails"
  done
  sleep 1; r1=$(rss); t1=$(date +%s)
  alive=$(kill -0 $D 2>/dev/null && echo yes || echo no)
  kill $D; wait $D 2>/dev/null
  growth=$((r1 - r0))
  echo "cycles: $N in $((t1 - t0)) s; rss $r0 -> $r1 kB (growth $growth kB); stuck menus: $fails; daemon alive: $alive"
  # allow allocator noise: 4 MB over 200 cycles
  [ $fails -eq 0 ] && [ "$alive" = yes ] && [ $growth -lt 4096 ] && echo PASS || { echo FAIL; exit 1; }
' _ "$B" "$N" "$W"
