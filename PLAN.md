# Plan

Follows the SRS §16 phases. Status as of 2026-11-10.

| Phase | Goal | Status |
|---|---|---|
| A: Discover and design | Research, protocol notes, parity matrix, ADRs | Done: [docs/research-report.md](docs/research-report.md), [hardware-protocol.md](docs/hardware-protocol.md), [platform-capabilities.md](docs/platform-capabilities.md), [ADRs](docs/architecture-decisions/). Web sources still to be rechecked before release |
| B: Vertical slice | User service, device read, raw events in GUI/CLI, volume, haptics toggle | Implemented and tested on simulated input; **physical capture pending** |
| C: Windows parity | Radial menu, standard tools, custom CW/CCW/click tools, haptic intervals, sensitivity, editor GUI | Implemented; overlay and key injection verified on Wayland (nested Hyprland, simulated input); **hardware pending** |
| D: Profiles and plugins | Per-app switching, macros, app integrations, restricted-Wayland fallbacks | Implemented. Focus trackers for Hyprland (tested), Sway, wlroots, X11, KWin and GNOME (untested); COSMIC manual only. Krita plugin and Rust example not run |
| E: Packaging and hardening | Packages, docs, diagnostics, security review, CI | .deb and Arch packages built, tested and installed in containers; threat model; CI workflows. **Physical installs per distro, soak tests and signed releases pending** |

## Release gate for 1.0

1. Run [tests/hardware/README.md](tests/hardware/README.md) on the OS matrix. Every P0 row must be `verified`, or have a published limitation.
2. Recheck the Microsoft and upstream sources in the research report.
3. Trademark check of the name "OpenDial".
4. Sign the release artifacts (needs the maintainer's key).
