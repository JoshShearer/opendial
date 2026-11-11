#!/bin/bash
# SRS §14.7 / hardware checklist 3.4 without hardware: in a nested Hyprland a
# real daemon starts holding Alt (window-switch tool, holdModifiers), is killed
# with SIGKILL mid-hold, and a fresh daemon then types "a" into a foot
# terminal. A stuck Alt would arrive as ESC a. Expects the first daemon's
# Alt+Tab (ESC TAB in foot), then plain "aaa" (3 detents) and a newline.
# Usage: $0 [WORKDIR]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
N=${1:-$(mktemp -d)}; mkdir -p "$N"; cd "$HERE/../.."
before=$(ls $XDG_RUNTIME_DIR/ | grep -E '^wayland-[0-9]+$' | sort)
Hyprland --config "$HERE/nested-hypr.conf" > $N/hypr.log 2>&1 &
H=$!; sleep 4
after=$(ls $XDG_RUNTIME_DIR/ | grep -E '^wayland-[0-9]+$' | sort)
WD=$(comm -13 <(echo "$before") <(echo "$after") | head -1)
SIG=$(ls -t $XDG_RUNTIME_DIR/hypr | head -1)
export WAYLAND_DISPLAY=$WD HYPRLAND_INSTANCE_SIGNATURE=$SIG QT_QPA_PLATFORM=wayland XDG_CONFIG_HOME=$N/cfg XDG_STATE_HOME=$N/state QT_FORCE_STDERR_LOGGING=1 NO_AT_BRIDGE=1
unset DISPLAY
foot sh -c "stty -echo; cat > $N/typed.txt" > $N/foot.log 2>&1 &
F=$!; sleep 2
dbus-run-session -- bash -c "
  C=./build/src/cli/opendialctl
  ./build/src/daemon/opendiald --no-device --no-tray --no-overlay > $N/daemon1.log 2>&1 &
  D=\$!; sleep 2
  \$C config get > $N/c.json
  python3 - $N/c.json <<'PY'
import json,sys
p=sys.argv[1]; c=json.load(open(p))
c['profiles'][0]['tools'].append({'id':'typer','name':'Typer','bindings':{
 'rotate_cw':{'type':'key','keys':'a'},'click':{'type':'key','keys':'Return'}}})
json.dump(c,open(p,'w'))
PY
  \$C config set $N/c.json >/dev/null
  \$C tool windows
  \$C simulate rotate 30; sleep 0.2    # Alt is now held for 900 ms
  kill -9 \$D; wait \$D 2>/dev/null
  sleep 0.5
  ./build/src/daemon/opendiald --no-device --no-tray --no-overlay > $N/daemon2.log 2>&1 &
  D=\$!; sleep 2
  hyprctl dispatch focuswindow class:foot >/dev/null
  \$C tool typer
  \$C simulate rotate 30; sleep 0.4
  \$C simulate click; sleep 0.8
  kill \$D; wait \$D 2>/dev/null
"
sleep 0.5; kill $F 2>/dev/null; sleep 0.3
echo "typed: [$(od -c $N/typed.txt | head -2)]"
hyprctl dispatch exit >/dev/null 2>&1; sleep 1; kill $H 2>/dev/null
python3 - "$N/typed.txt" <<'PY' && { echo PASS; exit 0; } || { echo FAIL; exit 1; }
import sys
t = open(sys.argv[1], 'rb').read() if __import__('os').path.exists(sys.argv[1]) else b''
after = t.split(b'\x1b\t', 1)[-1]   # what the second daemon typed
sys.exit(0 if after == b'aaa\n' else 1)
PY
