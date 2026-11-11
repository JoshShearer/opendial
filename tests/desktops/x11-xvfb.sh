#!/bin/bash
# X11 desktop check in a clean Arch container, no hardware: Xvfb + Openbox
# (sets _NET_ACTIVE_WINDOW), the built package, an xterm running `cat > file`.
# Checks the EWMH focus tracker and that a real (not dry-run) daemon types into
# the window through XTest. Simulated input only: not hardware evidence.
# Usage: tests/desktops/x11-xvfb.sh PACKAGE.pkg.tar.zst [OUTDIR]
set -eu
pkg=$(readlink -f "$1"); out=$(readlink -f "${2:-$(mktemp -d)}"); mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
docker run --rm -v "$pkg:/pkg/$(basename "$pkg"):ro" -v "$here:/t:ro" -v "$out:/out" archlinux:latest bash -c '
  set -e
  pacman -Syu --noconfirm --needed xorg-server-xvfb openbox xterm ttf-dejavu python dbus >/out/pacman.log 2>&1
  pacman -U --noconfirm /pkg/*.pkg.tar.zst >>/out/pacman.log 2>&1
  export XDG_RUNTIME_DIR=/tmp/xdg HOME=/root XDG_SESSION_TYPE=x11 QT_FORCE_STDERR_LOGGING=1
  mkdir -m700 -p $XDG_RUNTIME_DIR
  dbus-run-session -- bash /t/x11-inner.sh
'
