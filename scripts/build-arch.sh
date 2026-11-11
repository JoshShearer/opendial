#!/bin/sh
# Build the Arch package from the working tree: scripts/build-arch.sh [makepkg args]
set -eu
src=$(cd "$(dirname "$0")/.." && pwd)
ver=$(sed -n 's/^pkgver=//p' "$src/packaging/arch/PKGBUILD")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$src/packaging/arch/PKGBUILD" "$src/packaging/arch/opendial.install" "$work/"
mkdir "$work/opendial-$ver"
git -C "$src" ls-files -co --exclude-standard | tar -C "$src" -cf - -T - | tar -C "$work/opendial-$ver" -xf -
tar -C "$work" -czf "$work/opendial-$ver.tar.gz" "opendial-$ver"
rm -rf "$work/opendial-$ver"
(cd "$work" && makepkg -f "$@")
mkdir -p "$src/dist"
cp "$work"/*.pkg.tar.* "$src/dist/"
ls -l "$src/dist"/*.pkg.tar.*
