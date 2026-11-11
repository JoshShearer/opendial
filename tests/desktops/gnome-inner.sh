#!/bin/bash
# Runs inside the container's private session bus (see gnome-headless.sh).
set -u
gsettings set org.gnome.shell disable-user-extensions false
gsettings set org.gnome.shell enabled-extensions "['opendial@opendial.github.io']"
gnome-shell --version
gnome-shell --unsafe-mode --headless --wayland --virtual-monitor 1280x800 > /out/shell.log 2>&1 &
sleep 10
export WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland
opendiald --no-device --no-tray > /out/daemon.log 2>&1 &
D=$!; sleep 3
foot -a opendial-test-term sh -c 'sleep 30' > /out/foot.log 2>&1 &
sleep 4
# Headless GNOME has had no user input, so it does not focus new windows;
# focus the terminal the way a click would (Eval needs --unsafe-mode).
gdbus call --session -d org.gnome.Shell -o /org/gnome/Shell -m org.gnome.Shell.Eval \
  "global.display.list_all_windows().find(w => w.get_wm_class() === 'opendial-test-term').activate(global.get_current_time())" > /out/eval.txt 2>&1
sleep 2
gnome-extensions info opendial@opendial.github.io > /out/ext.txt 2>&1
gdbus call --session -d org.gnome.Shell -o /org/gnome/Shell/Introspect -m org.gnome.Shell.Introspect.GetWindows > /out/windows.txt 2>&1
opendialctl --json status > /out/status.json
kill $D; wait $D 2>/dev/null
python3 - <<'PY'
import json, sys
s = json.load(open('/out/status.json'))
ctx = s['capabilities']['context']
print('trackers:', json.dumps(ctx.get('trackers')))
print('focus tracker:', ctx['active'], '->', json.dumps(ctx['focus'].get('appId')), 'source', ctx['focus'].get('source'))
ok = ctx['focus'].get('appId') == 'opendial-test-term'
print('PASS focus' if ok else 'FAIL focus')
sys.exit(0 if ok else 1)
PY
