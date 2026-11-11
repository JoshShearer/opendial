#!/bin/bash
# Runs inside the container's private session bus (see kwin-headless.sh).
set -u
kwin_wayland --virtual --no-lockscreen --socket wayland-kwin --width 1280 --height 800 > /out/kwin.log 2>&1 &
sleep 6
export WAYLAND_DISPLAY=wayland-kwin QT_QPA_PLATFORM=wayland
foot -a opendial-test-term sh -c 'stty -echo; cat > /out/typed.txt' > /out/foot.log 2>&1 &
sleep 3
opendiald --no-device --no-tray > /out/daemon.log 2>&1 &
D=$!; sleep 4
C=opendialctl
$C --json status > /out/status.json
$C config get > /out/c.json
python3 - /out/c.json <<'PY'
import json,sys
p=sys.argv[1]; c=json.load(open(p))
c['profiles'][0]['tools'].append({'id':'typer','name':'Typer','bindings':{
 'rotate_cw':{'type':'key','keys':'a'},'rotate_ccw':{'type':'key','keys':'Shift+b'},'click':{'type':'key','keys':'Return'}}})
json.dump(c,open(p,'w'))
PY
$C config set /out/c.json > /dev/null
$C tool typer
$C simulate rotate 30; sleep 0.5
$C simulate rotate -20; sleep 0.5
$C simulate click; sleep 1
$C --json status > /out/status-after.json
kill $D; wait $D 2>/dev/null
kwin_wayland --version 2>/dev/null | head -1
python3 - <<'PY'
import json, sys
s = json.load(open('/out/status.json'))
ctx = s['capabilities']['context']
inj = s['capabilities']['injection']
typed = open('/out/typed.txt').read() if __import__('os').path.exists('/out/typed.txt') else ''
print('focus tracker:', ctx['active'], '->', json.dumps(ctx['focus'].get('appId')), 'exe', ctx['focus'].get('exe'))
print('keyboard injection:', json.dumps(inj.get('activeKeyboard')), '| typed:', repr(typed))
ok = ctx['active'] == 'kwin-script' and ctx['focus'].get('appId') == 'opendial-test-term'
print('PASS focus' if ok else 'FAIL focus')
# KWin keeps zwp_virtual_keyboard for trusted clients; keys go through the
# RemoteDesktop portal, which needs a person to accept it. Not asserted here.
sys.exit(0 if ok else 1)
PY
