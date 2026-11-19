# Installing OpenDial

| Distribution | Guide |
|---|---|
| Ubuntu 24.04 LTS and newer, Pop!_OS 24.04 (COSMIC or GNOME), Debian 13 | [ubuntu-popos.md](ubuntu-popos.md) |
| Arch Linux and derivatives | [arch.md](arch.md) |
| Anything else, or development builds | [from-source.md](from-source.md) |
| Removing it | [uninstall.md](uninstall.md) |

## After installing (all distributions)

1. **Pair the Dial.** Pull off the bottom cover, then hold the pairing button next to the batteries for about 5 s until the LED flashes. Then pair it in your desktop's Bluetooth settings, or on the app's *Device* page (Search → Pair). Mark it *trusted* so it reconnects by itself.
2. **Start the service** as your normal user (never with sudo):
   ```sh
   systemctl --user enable --now opendial.service
   ```
   Packages enable it for all users, so after the next login this is automatic. D-Bus activation also starts it when the app or `opendialctl` needs it.
3. **Open "OpenDial"** from the application menu. The first-run wizard checks device access and input injection, then asks you to turn and click once.
4. **Desktop-specific steps:**
   - **GNOME (Wayland):**
     - Allow the "Remote desktop" input permission when asked. It is asked once and remembered.
     - For per-app profiles and opening the menu at the pointer, enable the bundled extension:
       ```sh
       mkdir -p ~/.local/share/gnome-shell/extensions
       cp -r /usr/share/opendial/plugins/gnome-shell/opendial@joshshearer.github.io ~/.local/share/gnome-shell/extensions/
       # log out and back in, then:
       gnome-extensions enable opendial@joshshearer.github.io
       ```
   - **KDE Plasma:** nothing to install. OpenDial loads a small KWin script over D-Bus for focus tracking.
   - **COSMIC:** profiles switch manually (Dashboard → lock a profile) until COSMIC offers a focus API. See [platform capabilities](../platform-capabilities.md).
   - **Krita:** see [plugins/krita](../../plugins/krita/README.md).

## Device permissions

The package installs `/usr/lib/udev/rules.d/70-opendial-surface-dial.rules`.
It gives the logged-in seat access to the Surface Dial's own input and hidraw
nodes and nothing else. If the Dial was already connected while you installed,
turn it off and on again, or run:
```sh
sudo udevadm trigger --subsystem-match=hidraw --subsystem-match=input --action=change
```
Check it with `opendialctl --json devices`: `evdevReadable` and `hidrawWritable` should both be `true`.

## Offline install

Both packages are self-contained apart from distribution dependencies. Copy
the `.deb` or `.pkg.tar.zst` file plus the dependencies the package manager
reports (`apt-get install --print-uris ./opendial_*.deb`, or
`pacman -Sp $(pacman -Qip opendial-*.pkg.tar.zst | sed -n 's/^Depends On *: //p')`)
to the target machine, and install from the local files.

## Upgrades

Upgrades never touch `~/.config/opendial`. Configs from older versions are
migrated on first load, and the original is kept as a backup. A config from a
*newer* version is opened read-only, never overwritten. After upgrading, restart
the service: `systemctl --user restart opendial`.
