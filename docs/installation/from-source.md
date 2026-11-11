# Building from source

Requirements: CMake ≥ 3.22, a C++20 compiler (GCC 12+ / Clang 15+), Qt ≥ 6.4
(Core, Gui, Widgets, DBus, Network, Quick, QuickControls2, Svg; Test for tests),
libudev, xkbcommon. Optional: wayland-client + wayland-scanner (native Wayland
input and focus), libX11 + libXtst (X11), LayerShellQt (overlay on
wlroots/KDE/COSMIC).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
sudo cmake --install build
```

| CMake option | Default | |
|---|---|---|
| `OPENDIAL_BUILD_TESTS` | ON | unit + integration tests |
| `OPENDIAL_BUILD_GUI` | ON | the `opendial` app |
| `OPENDIAL_WITH_WAYLAND` / `_X11` / `_LAYERSHELL` | ON | backends, auto-disabled if libraries are missing |
| `OPENDIAL_WERROR` | OFF | warnings as errors (CI) |
| `OPENDIAL_UDEV_RULES_DIR` | `<prefix>/lib/udev/rules.d` | |
| `OPENDIAL_SYSTEMD_USER_UNIT_DIR` | `<prefix>/lib/systemd/user` | |

## Running without installing

```sh
./build/src/daemon/opendiald            # the service, in the foreground
./build/src/ui/opendial                 # the app
./build/src/cli/opendialctl status
```
Safe experimentation (no real input injected, private bus, scratch config):
```sh
XDG_CONFIG_HOME=/tmp/od XDG_STATE_HOME=/tmp/od dbus-run-session -- \
  ./build/src/daemon/opendiald --dry-run --no-device
```
Without installing the udev rule, the Dial is readable if you are in the
`input` group, and haptics stay unavailable. To install just the rule:
`sudo install -m644 packaging/udev/70-opendial-surface-dial.rules /etc/udev/rules.d/`.
