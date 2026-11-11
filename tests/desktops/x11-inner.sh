#!/bin/bash
# Runs inside the container's private session bus (see x11-xvfb.sh).
set -u
Xvfb :5 -screen 0 1280x800x24 > /out/xvfb.log 2>&1 &
sleep 2
export DISPLAY=:5 QT_QPA_PLATFORM=xcb
openbox > /out/openbox.log 2>&1 &
sleep 1
xterm -class OpenDialTestTerm -e sh -c 'stty -echo; cat > /out/typed.txt' &
sleep 2
opendiald --no-device --no-tray > /out/daemon.log 2>&1 &
D=$!; sleep 3
C=opendialctl
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
$C --json status > /out/status.json
kill $D; wait $D 2>/dev/null
python3 - <<'PY'
import json, os, sys
s = json.load(open('/out/status.json'))
ctx = s['capabilities']['context']; inj = s['capabilities']['injection']
typed = open('/out/typed.txt').read() if os.path.exists('/out/typed.txt') else ''
print('focus tracker:', ctx['active'], '->', json.dumps(ctx['focus'].get('appId')), '| class', ctx['focus'].get('windowClass'))
print('keyboard injection:', json.dumps(inj.get('activeKeyboard')), '| typed:', repr(typed))
focus = 'opendialtestterm' in (ctx['focus'].get('appId','') + ctx['focus'].get('windowClass','')).lower()
keys = typed == 'aaaBB\n'
print(('PASS' if focus else 'FAIL') + ' focus;', ('PASS' if keys else 'FAIL') + ' keys')
sys.exit(0 if focus and keys else 1)
PY
