#!/bin/sh
# Build the .deb from a clean copy of the tree: scripts/build-deb.sh [OUTDIR]
# Needs the Build-Depends from packaging/debian/control (or use the
# Dockerfile in packaging/docker).
set -eu
src=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$src/dist}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$out" "$work/opendial"
if git -C "$src" rev-parse >/dev/null 2>&1; then
    git -C "$src" ls-files -co --exclude-standard | tar -C "$src" -cf - -T - | tar -C "$work/opendial" -xf -
else
    tar -C "$src" --exclude=./build --exclude=./dist -cf - . | tar -C "$work/opendial" -xf -
fi
cp -r "$work/opendial/packaging/debian" "$work/opendial/debian"
(cd "$work/opendial" && dpkg-buildpackage -us -uc -b)
cp "$work"/*.deb "$out"/
ls -l "$out"/*.deb
