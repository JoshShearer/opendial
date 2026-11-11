#!/bin/bash
# KDE (KWin 6) desktop check in a clean Arch container, no hardware:
# installs the built package, runs kwin_wayland --virtual, then checks that
# OpenDial's KWin focus tracker sees the focused app and that a real (not
# dry-run) daemon types into it. Simulated input only: not hardware evidence.
# Needs a GPU render node (/dev/dri is passed in): KWin 6 has no CPU renderer.
# Usage: tests/desktops/kwin-headless.sh PACKAGE.pkg.tar.zst [OUTDIR]
set -eu
pkg=$(readlink -f "$1"); out=$(readlink -f "${2:-$(mktemp -d)}"); mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
docker run --rm --device /dev/dri -v "$pkg:/pkg/$(basename "$pkg"):ro" -v "$here:/t:ro" -v "$out:/out" archlinux:latest bash -c '
  set -e
  pacman -Syu --noconfirm --needed kwin foot ttf-dejavu python dbus >/out/pacman.log 2>&1
  pacman -U --noconfirm /pkg/*.pkg.tar.zst >>/out/pacman.log 2>&1
  # kwin_wayland carries cap_sys_nice, which an unprivileged container refuses to exec
  setcap -r /usr/bin/kwin_wayland
  export XDG_RUNTIME_DIR=/tmp/xdg HOME=/root XDG_CURRENT_DESKTOP=KDE KDE_SESSION_VERSION=6 QT_FORCE_STDERR_LOGGING=1
  mkdir -m700 -p $XDG_RUNTIME_DIR
  dbus-run-session -- bash /t/kwin-inner.sh
'
