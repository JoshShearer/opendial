#!/bin/sh
# Run inside ubuntu:24.04 (or debian:trixie) with the tree at /src:
#   docker run --rm -v "$PWD":/src:ro -v "$PWD/dist":/out ubuntu:24.04 sh /src/packaging/docker/deb.sh
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential devscripts equivs git ca-certificates lintian >/dev/null
cp -r /src /build && cd /build && git config --global --add safe.directory '*'
mk-build-deps -i -r -t 'apt-get -y -qq --no-install-recommends' packaging/debian/control >/dev/null
rm -f opendial-build-deps_*
scripts/build-deb.sh /build/dist
echo "== lintian"; lintian --no-tag-display-limit /build/dist/*.deb || true
apt-get install -y -qq /build/dist/opendial_*.deb >/dev/null
echo "== installed"; dpkg -L opendial | grep -E 'bin/|udev|systemd|dbus-1|applications'
opendialctl --version; opendiald --help | head -3
cp /build/dist/*.deb /out/
