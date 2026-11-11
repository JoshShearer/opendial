# Ubuntu, Pop!_OS and Debian

Tested so far:
- Ubuntu 24.04 (Qt 6.4.2): package built, test suite passed, package installed in a clean container (2026-11-10).
- Not yet installed on a physical Ubuntu or Pop!_OS desktop. See [VERIFICATION.md](../../VERIFICATION.md).

## Build the package

```sh
sudo apt install devscripts equivs git
git clone <repository> opendial && cd opendial
sudo mk-build-deps -i -r packaging/debian/control
scripts/build-deb.sh            # → dist/opendial_0.9.0-1_amd64.deb
```
To build in a throw-away container instead (needs Docker, nothing is installed
on your system):
```sh
docker run --rm -v "$PWD":/src:ro -v "$PWD/dist":/out ubuntu:24.04 sh /src/packaging/docker/deb.sh
```

## Install

```sh
sudo apt install ./dist/opendial_0.9.0-1_amd64.deb
systemctl --user enable --now opendial.service
```
Then follow [After installing](README.md#after-installing-all-distributions).

## Notes

- Ubuntu 24.04 has no Qt 6 LayerShellQt, so the radial menu is a frameless window. That is fine on GNOME. On Pop!_OS COSMIC, a build against a newer LayerShellQt gives the layer-shell overlay.
- Recommended packages: `bluez`, `wireplumber` (or `pulseaudio-utils`), `xdg-desktop-portal`. Optional: `brightnessctl`, `ddcutil` (external monitor brightness), `python3-gi` (Python plugin example).
