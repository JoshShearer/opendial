# Uninstalling

```sh
systemctl --user disable --now opendial.service
sudo apt remove opendial          # Debian/Ubuntu/Pop!_OS
sudo pacman -R opendial           # Arch
```
This removes the binaries, the udev rule, the user unit, the D-Bus service
file, the desktop entry and the bundled plugins. Nothing privileged stays
behind: there is no helper, setuid binary or system service.

**Your settings stay**, even with `apt purge`: they belong to you, not to the
package. To remove them:
```sh
rm -rf ~/.config/opendial ~/.local/state/opendial
```
The GNOME extension, if you copied it: `rm -rf ~/.local/share/gnome-shell/extensions/opendial@opendial.github.io`.
The Krita plugin: remove `opendial` and `opendial.desktop` from `~/.local/share/krita/pykrita/`.
If you installed the optional uinput rule: `sudo rm /etc/udev/rules.d/71-opendial-uinput-optin.rules`.
