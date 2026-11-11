#!/bin/bash
# Krita plugin check in a clean Arch container, no hardware: real Krita on
# Xvfb with OpenDial's Python plugin enabled, and a real daemon. Checks that
# the plugin registers its tools, that Dial turns reach it, and that it changes
# the brush size through Krita's API (seen in its OSD feedback).
# Simulated Dial input only: not hardware evidence.
# Usage: tests/desktops/krita-xvfb.sh PACKAGE.pkg.tar.zst [OUTDIR]
set -eu
pkg=$(readlink -f "$1"); out=$(readlink -f "${2:-$(mktemp -d)}"); mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
docker run --rm -v "$pkg:/pkg/$(basename "$pkg"):ro" -v "$here:/t:ro" -v "$out:/out" archlinux:latest bash -c '
  set -e
  pacman -Syu --noconfirm --needed krita python-pyqt6 xorg-server-xvfb openbox ttf-dejavu python dbus >/out/pacman.log 2>&1
  pacman -U --noconfirm /pkg/*.pkg.tar.zst >>/out/pacman.log 2>&1
  pacman -Q krita | tee /out/krita-version.txt
  export XDG_RUNTIME_DIR=/tmp/xdg HOME=/root XDG_SESSION_TYPE=x11 QT_FORCE_STDERR_LOGGING=1
  mkdir -m700 -p $XDG_RUNTIME_DIR $HOME/.local/share/krita/pykrita $HOME/.config
  cp -r /usr/share/opendial/plugins/krita/opendial /usr/share/opendial/plugins/krita/opendial.desktop $HOME/.local/share/krita/pykrita/
  printf "[python]\nenable_opendial=true\n" > $HOME/.config/kritarc
  dbus-run-session -- bash /t/krita-inner.sh
'
