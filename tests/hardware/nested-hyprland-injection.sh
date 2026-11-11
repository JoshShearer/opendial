#!/bin/bash
# Wayland key-injection E2E in a nested Hyprland (no hardware): a real daemon
# types into a foot terminal; expects "aaaBB<newline>". Usage: $0 [WORKDIR]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
N=${1:-$(mktemp -d)}; mkdir -p "$N"; cd "$HERE/../.."
before=$(ls ${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/ | grep -E '^wayland-[0-9]+$' | sort)
Hyprland --config "$HERE/nested-hypr.conf" > $N/hypr.log 2>&1 &
H=$!; sleep 4
after=$(ls ${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/ | grep -E '^wayland-[0-9]+$' | sort)
WD=$(comm -13 <(echo "$before") <(echo "$after") | head -1)
SIG=$(ls -t ${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/hypr | head -1)
echo "nested display=$WD sig=$SIG"
export WAYLAND_DISPLAY=$WD HYPRLAND_INSTANCE_SIGNATURE=$SIG QT_QPA_PLATFORM=wayland XDG_CONFIG_HOME=$N/cfg XDG_STATE_HOME=$N/state QT_FORCE_STDERR_LOGGING=1 NO_AT_BRIDGE=1
unset DISPLAY
foot -o 'main.font=monospace:size=10' sh -c "stty -echo; cat > $N/typed.txt" > $N/foot.log 2>&1 &
F=$!; sleep 2
dbus-run-session -- bash -c "
  ./build/src/daemon/opendiald --no-device --no-tray > $N/daemon.log 2>&1 &
  D=\$!; sleep 2
  C=./build/src/cli/opendialctl
  \$C config get > $N/c.json
  python3 - $N/c.json <<'PY'
import json,sys
p=sys.argv[1]; c=json.load(open(p))
g=c['profiles'][0]
g['tools'].append({'id':'typer','name':'Typer','bindings':{
 'rotate_cw':{'type':'key','keys':'a'},'rotate_ccw':{'type':'key','keys':'Shift+b'},'click':{'type':'key','keys':'Return'}}})
json.dump(c,open(p,'w'))
PY
  \$C config set $N/c.json
  \$C tool typer
  \$C --json status | python3 -c 'import json,sys; s=json.load(sys.stdin); print(\"injection:\", json.dumps(s.get(\"capabilities\",{}).get(\"injection\",{}).get(\"activeKeyboard\")))'
  \$C simulate rotate 30; sleep 0.5
  \$C simulate rotate -20; sleep 0.5
  \$C simulate click; sleep 1
  kill \$D; wait \$D
"
sleep 0.5
kill $F 2>/dev/null; sleep 0.3
echo "typed: [$(od -c $N/typed.txt | head -3)]"
RESULT=$(cat $N/typed.txt 2>/dev/null)
HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl dispatch exit >/dev/null 2>&1; sleep 1; kill $H 2>/dev/null
[ "$RESULT" = "aaaBB" ] && { echo PASS; exit 0; } || { echo FAIL; exit 1; }
