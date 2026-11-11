#!/bin/bash
# COSMIC check without a COSMIC install on the host: runs cosmic-comp in an
# Arch container as a window inside a nested Hyprland (winit backend), then
# lists the Wayland globals it offers clients and runs the daemon against it
# to see which OpenDial backends come up. Simulated, not hardware evidence.
# Usage: tests/desktops/cosmic-probe.sh PACKAGE.pkg.tar.zst [OUTDIR]
set -u
pkg=$(readlink -f "$1"); out=$(readlink -f "${2:-$(mktemp -d)}"); mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
before=$(ls $XDG_RUNTIME_DIR/ | grep -E '^wayland-[0-9]+$' | sort)
Hyprland --config "$here/../hardware/nested-hypr.conf" > $out/hypr.log 2>&1 &
H=$!; sleep 4
after=$(ls $XDG_RUNTIME_DIR/ | grep -E '^wayland-[0-9]+$' | sort)
WD=$(comm -13 <(echo "$before") <(echo "$after") | head -1)
SIG=$(ls -t $XDG_RUNTIME_DIR/hypr | head -1)
docker run --rm --device /dev/dri -v "$XDG_RUNTIME_DIR/$WD:/host-wayland" -v "$pkg:/pkg/$(basename "$pkg"):ro" -v "$out:/out" -v "$here:/t:ro" archlinux:latest bash -c '
  pacman -Syu --noconfirm --needed cosmic-comp wayland-utils foot ttf-dejavu python dbus mesa >/out/pacman.log 2>&1
  pacman -U --noconfirm /pkg/*.pkg.tar.zst >>/out/pacman.log 2>&1
  pacman -Q cosmic-comp | tee /out/cosmic-version.txt
  export XDG_RUNTIME_DIR=/tmp/xdg HOME=/root QT_FORCE_STDERR_LOGGING=1
  mkdir -m700 -p $XDG_RUNTIME_DIR; ln -s /host-wayland $XDG_RUNTIME_DIR/host-wayland
  dbus-run-session -- bash -c "
    WAYLAND_DISPLAY=host-wayland cosmic-comp > /out/cosmic.log 2>&1 &
    sleep 8
    W=\$(ls \$XDG_RUNTIME_DIR | grep -E \"^wayland-[0-9]+\$\" | head -1); echo cosmic display: \$W
    export WAYLAND_DISPLAY=\$W XDG_CURRENT_DESKTOP=COSMIC QT_QPA_PLATFORM=wayland
    wayland-info > /out/globals.txt 2>&1
    foot -a opendial-test-term sh -c \"stty -echo; cat > /out/typed.txt\" > /out/foot.log 2>&1 &
    sleep 2
    opendiald --no-device --no-tray > /out/daemon.log 2>&1 & D=\$!; sleep 4
    opendialctl --json status > /out/status.json
    opendialctl config get > /out/c.json
    python3 /t/add-typer.py /out/c.json
    opendialctl config set /out/c.json >/dev/null; opendialctl tool typer
    opendialctl simulate rotate 30; sleep 0.5; opendialctl simulate rotate -20; sleep 0.5; opendialctl simulate click; sleep 1
    foot -a opendial-second-term sh -c \"sleep 10\" > /out/foot2.log 2>&1 &
    sleep 3
    opendialctl --json status > /out/status2.json
    kill \$D
  "
'
HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl dispatch exit >/dev/null 2>&1; sleep 1; kill $H 2>/dev/null
grep -oE "interface: '[a-z0-9_]+'" $out/globals.txt | sort -u | sed "s/interface: //" > $out/interfaces.txt
echo "cosmic-comp: $(cat $out/cosmic-version.txt 2>/dev/null)"
echo "typed: [$(od -c $out/typed.txt 2>/dev/null | head -1)]"
[ "$(cat $out/typed.txt 2>/dev/null)" = "aaaBB" ] && echo "PASS keys" || echo "FAIL keys"
python3 - "$out/status.json" "$out/status2.json" <<'PY'
import json, sys
ctx = json.load(open(sys.argv[1]))['capabilities']['context']
print('focus tracker:', ctx['active'], '->', json.dumps(ctx['focus'].get('appId')))
print('PASS focus' if ctx['focus'].get('appId') == 'opendial-test-term' else 'FAIL focus')
ctx2 = json.load(open(sys.argv[2]))['capabilities']['context']
print('after opening a second window ->', json.dumps(ctx2['focus'].get('appId')))
print('PASS focus change' if ctx2['focus'].get('appId') == 'opendial-second-term' else 'FAIL focus change')
PY
for i in zwp_virtual_keyboard_manager_v1 zwlr_virtual_pointer_manager_v1 zwlr_foreign_toplevel_manager_v1 ext_foreign_toplevel_list_v1 zcosmic_toplevel_info_v1 zwlr_layer_shell_v1; do
  grep -q "'$i'" $out/interfaces.txt && echo "  offers $i" || echo "  lacks  $i"
done
