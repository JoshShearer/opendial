#!/bin/bash
# Runs inside the container's private session bus (see krita-xvfb.sh).
set -u
Xvfb :5 -screen 0 1600x1000x24 > /out/xvfb.log 2>&1 &
sleep 2
export DISPLAY=:5
openbox > /out/openbox.log 2>&1 &
QT_QPA_PLATFORM=xcb opendiald --no-device --no-tray > /out/daemon.log 2>&1 &
D=$!; sleep 3
dbus-monitor "type='method_call',interface='io.github.opendial.Daemon1'" > /out/monitor.txt 2>&1 &
M=$!
krita --nosplash --new-image RGBA,U8,800,600 > /out/krita.log 2>&1 &
K=$!
for i in $(seq 1 60); do opendialctl plugins 2>/dev/null | grep -q org.kde.krita && break; sleep 1; done
opendialctl plugins > /out/plugins.txt 2>&1
opendialctl plugin allow org.kde.krita
sleep 2   # let Krita finish opening the document
opendialctl context org.kde.krita
opendialctl tools > /out/tools.txt 2>&1
opendialctl tool plugin:org.kde.krita:brush-size
opendialctl simulate rotate 30; sleep 1
opendialctl simulate rotate -12; sleep 1
opendialctl tool plugin:org.kde.krita:rotate
opendialctl simulate rotate 18; sleep 1
kill $K $M; kill $D; wait 2>/dev/null
python3 - <<'PY'
import re, sys
plugins = open('/out/plugins.txt').read()
mon = open('/out/monitor.txt').read()
osd = re.findall(r'string "(\{\\?"osd\\?".*?)"\n', mon) or re.findall(r'"osd".{0,120}', mon)
print('registered:', 'org.kde.krita' in plugins)
for o in osd: print('feedback:', o[:140])
ok = 'org.kde.krita' in plugins and any('Brush size' in o for o in osd) and any('Canvas angle' in o for o in osd)
print('PASS krita plugin' if ok else 'FAIL krita plugin')
sys.exit(0 if ok else 1)
PY
