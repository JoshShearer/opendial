#!/bin/bash
# GNOME Shell desktop check in a clean Arch container, no hardware: installs
# the built package and OpenDial's Shell extension, runs gnome-shell
# --headless, and checks that the extension reports the focused app to the
# daemon. Simulated input only: not hardware evidence.
# Needs a GPU render node (/dev/dri is passed in).
# Usage: tests/desktops/gnome-headless.sh PACKAGE.pkg.tar.zst [OUTDIR]
set -eu
pkg=$(readlink -f "$1"); out=$(readlink -f "${2:-$(mktemp -d)}"); mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
docker run --rm --device /dev/dri -v "$pkg:/pkg/$(basename "$pkg"):ro" -v "$here:/t:ro" -v "$out:/out" archlinux:latest bash -c '
  set -e
  pacman -Syu --noconfirm --needed gnome-shell foot ttf-dejavu python python-gobject dbus libcap >/out/pacman.log 2>&1
  pacman -U --noconfirm /pkg/*.pkg.tar.zst >>/out/pacman.log 2>&1
  for b in /usr/bin/gnome-shell /usr/bin/mutter; do [ -e $b ] && setcap -r $b 2>/dev/null || true; done
  export XDG_RUNTIME_DIR=/tmp/xdg HOME=/root XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=GNOME QT_FORCE_STDERR_LOGGING=1
  mkdir -m700 -p $XDG_RUNTIME_DIR
  # GNOME Shell asks logind over the system bus at startup
  dbus-uuidgen --ensure; dbus-uuidgen --ensure=/etc/machine-id; mkdir -p /run/dbus
  # (a permissive bus plus a logind stand-in: the container has no systemd)
  dbus-daemon --config-file=/usr/share/dbus-1/session.conf --address=unix:path=/run/dbus/system_bus_socket --fork
  export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket XDG_SESSION_ID=c1
  python3 /t/fake-logind.py & sleep 1
  mkdir -p $HOME/.local/share/gnome-shell/extensions
  cp -r /usr/share/opendial/plugins/gnome-shell/opendial@joshshearer.github.io $HOME/.local/share/gnome-shell/extensions/
  dbus-run-session -- bash /t/gnome-inner.sh
'
