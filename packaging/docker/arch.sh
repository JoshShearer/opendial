#!/bin/sh
# Run inside archlinux:latest with the source tree mounted read-only at /src:
#   docker run --rm -v "$PWD":/src:ro -v "$PWD/dist":/out archlinux:latest sh /src/packaging/docker/arch.sh
set -eu
pacman -Syu --noconfirm --needed base-devel git namcap cmake ninja wayland-protocols qt6-tools \
  qt6-base qt6-declarative qt6-svg qt6-wayland layer-shell-qt libxkbcommon libx11 libxtst wayland \
  python dbus >/dev/null
useradd -m builder
cp -r /src /home/builder/src && chown -R builder /home/builder/src
su builder -c 'cd /home/builder/src && git config --global --add safe.directory "*" && scripts/build-arch.sh --nodeps'
pkg=$(ls /home/builder/src/dist/*.pkg.tar.zst | grep -v debug | head -1)
echo "== namcap PKGBUILD"; namcap /home/builder/src/packaging/arch/PKGBUILD || true
echo "== namcap package"; namcap "$pkg" || true
pacman -U --noconfirm "$pkg" >/dev/null
echo "== installed files"; pacman -Ql opendial | grep -E 'bin/|udev|systemd|dbus-1|applications' 
opendialctl --version; opendiald --help | head -3
cp /home/builder/src/dist/*.pkg.tar.zst /out/
