# Arch Linux

Tested so far:
- `archlinux:latest` container on 2026-11-10: `makepkg` (with `check()`, all tests passed), `namcap` (only expected warnings), `pacman -U`, binaries run.
- The reference machine (Arch-based, Hyprland) runs a development build.

## Build and install

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-svg \
     qt6-wayland qt6-tools layer-shell-qt wayland wayland-protocols libxkbcommon libx11 libxtst
scripts/build-arch.sh            # builds from your working tree → dist/
sudo pacman -U dist/opendial-0.9.0-1-x86_64.pkg.tar.zst
systemctl --user enable --now opendial.service
```
For a clean-chroot build (recommended before publishing):
```sh
cd packaging/arch && extra-x86_64-build    # from devtools
```
or in Docker: `docker run --rm -v "$PWD":/src:ro -v "$PWD/dist":/out archlinux:latest sh /src/packaging/docker/arch.sh`.

The PKGBUILD's `source=` points at the release tarball, so it can go to the
AUR once a release is tagged. Publishing to the AUR is a manual, reviewed step.
The `.SRCINFO` is generated with `makepkg --printsrcinfo > .SRCINFO`.

## namcap notes

These warnings are expected:
- `qt6-wayland` "may not be needed": it is loaded at runtime as a Qt platform plugin.
- The Python modules of the plugin examples: Krita, PyQt and gi are only needed by the optional plugins, which the `optdepends` cover.
