# Releasing OpenDial

The repository lives at https://github.com/JoshShearer/opendial. Releases are
cut from `main` by the maintainer.

## Before the first public release (v0.9.0)

- [x] IDs and URLs moved to `io.github.joshshearer.opendial` /
      `github.com/JoshShearer/opendial`.
- [x] Commit history authored with the GitHub noreply address.
- [x] Issue templates (hardware report, bug, feature), code of conduct,
      security policy.
- [x] README "Tested on" table matches `docs/parity-matrix.md`.
- [x] Redacted diagnostics hide device addresses, window titles and the home
      directory.
- [x] CI green on GitHub (all 7 jobs, 2026-11-19, 6a66cf4).
- [ ] Reboot check on the reference machine (optional for a 0.9 beta; required
      before 1.0).
- [x] Repository public (2026-11-21), private vulnerability reporting on, labels and topics set.

## Cutting a release

1. Make sure `main` is green in CI and the version is the same in
   `CMakeLists.txt`, `packaging/arch/PKGBUILD` (`pkgver`, `pkgrel=1`),
   `packaging/arch/.SRCINFO`, `packaging/debian/changelog` and the metainfo
   `<releases>` entry.
2. Tag and push:
   ```sh
   git tag -s v0.9.0 -m "OpenDial 0.9.0"    # or -a if you do not sign tags
   git push origin v0.9.0
   ```
3. Build the packages from the tagged tree:
   ```sh
   scripts/build-arch.sh                     # dist/opendial-0.9.0-1-x86_64.pkg.tar.zst
   scripts/build-deb.sh                      # dist/opendial_0.9.0-1_amd64.deb (Ubuntu 24.04 container or host)
   (cd dist && sha256sum opendial* > SHA256SUMS)
   ```
4. Create the GitHub release with the notes below and the files in `dist/`:
   ```sh
   gh release create v0.9.0 --prerelease --title "OpenDial 0.9.0 (beta)" \
     --notes-file docs/dev/release-notes-0.9.0.md dist/opendial* dist/SHA256SUMS
   ```

## AUR

The package name `opendial` was free on the AUR when checked on 2026-11-19.

1. Once the tag exists, put the real checksum in the PKGBUILD (the AUR does not
   accept `SKIP` for release tarballs):
   ```sh
   cd packaging/arch
   updpkgsums                 # from pacman-contrib; fills sha256sums from the tag tarball
   makepkg --printsrcinfo > .SRCINFO
   namcap PKGBUILD
   ```
   Commit the updated PKGBUILD and .SRCINFO to `main`.
2. Register an AUR account with an SSH key, then:
   ```sh
   git clone ssh://aur@aur.archlinux.org/opendial.git aur-opendial
   cp packaging/arch/PKGBUILD packaging/arch/.SRCINFO packaging/arch/opendial.install aur-opendial/
   cd aur-opendial && git add -A && git commit -m "opendial 0.9.0" && git push
   ```

## Other distros

- Ubuntu/Debian: attach the `.deb` to the GitHub release. A PPA can follow if
  people ask for it.
- Fedora: needs an RPM spec (not written yet); build it on COPR.
- Flatpak is not a fit: the daemon needs udev and direct device access.

## Announcing

Once the release is public: r/SurfaceLinux, the linux-surface community,
r/linux and the Omarchy community. Ask for hardware reports on GNOME, KDE and
COSMIC; the issue template collects what is needed.
