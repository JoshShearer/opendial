#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Plugin API (D-Bus) and local HTTP API end to end, using the shipped
examples: plugins/examples/python/dial_plugin.py and js/dial-events.mjs.
Run under dbus-run-session. Parts whose runtime is missing are skipped."""
import json, os, shutil, signal, subprocess, sys, tempfile, time, urllib.request, urllib.error

daemon, ctl, src = sys.argv[1], sys.argv[2], sys.argv[3]
tmp = tempfile.TemporaryDirectory()
port = 47000 + os.getpid() % 900
env = dict(os.environ, XDG_CONFIG_HOME=tmp.name + "/cfg", XDG_STATE_HOME=tmp.name + "/state",
           XDG_RUNTIME_DIR=tmp.name + "/run", QT_QPA_PLATFORM="offscreen", QT_FORCE_STDERR_LOGGING="1",
           OPENDIAL_HTTP_PORT=str(port))
os.makedirs(env["XDG_RUNTIME_DIR"], mode=0o700)
failures = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  ({detail})" if detail and not cond else ""), flush=True)
    if not cond:
        failures.append(name)


_queues = {}


def wait_line(proc, pred, timeout=5.0):
    """Next stdout line satisfying pred (other lines, e.g. hints, are skipped).
    A reader thread per process avoids select() missing buffered lines."""
    import queue, threading
    q = _queues.get(proc.pid)
    if q is None:
        q = _queues[proc.pid] = queue.Queue()
        threading.Thread(target=lambda: [q.put(l.strip()) for l in proc.stdout], daemon=True).start()
    end = time.time() + timeout
    while True:
        left = end - time.time()
        if left <= 0:
            return ""
        try:
            line = q.get(timeout=left)
        except queue.Empty:
            return ""
        if pred(line):
            return line


def octl(*a):
    return subprocess.run([ctl, *a], env=env, capture_output=True, text=True, timeout=30)


def status():
    return json.loads(octl("status", "--json").stdout)


# config with the HTTP API on
with open(os.path.join(src, "data", "default-config.json")) as f:
    cfg = json.load(f)
cfg["settings"]["integrations"]["httpApi"] = {"enabled": True, "port": port}
os.makedirs(env["XDG_CONFIG_HOME"] + "/opendial")
with open(env["XDG_CONFIG_HOME"] + "/opendial/config.json", "w") as f:
    json.dump(cfg, f)

d = subprocess.Popen([daemon, "--dry-run", "--no-device", "--no-context", "--no-overlay", "--no-tray"], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
procs = []
try:
    for _ in range(50):
        if octl("status").returncode == 0:
            break
        time.sleep(0.1)

    # ---------------- plugin over D-Bus (Python example)
    try:
        import gi  # noqa: F401
        have_gi = True
    except ImportError:
        have_gi = False
    if have_gi:
        plug = subprocess.Popen([sys.executable, "-u", os.path.join(src, "plugins/examples/python/dial_plugin.py")], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        procs.append(plug)
        line = wait_line(plug, lambda l: l.startswith("registered:"))
        check("plugin registers", bool(line))
        tools = json.loads(octl("tools", "--json").stdout)
        check("unapproved plugin tool hidden", not any(t["id"] == "plugin:org.example.counter:counter" for t in tools))
        check("approve plugin", octl("plugin", "allow", "org.example.counter").returncode == 0)
        tools = json.loads(octl("tools", "--json").stdout)
        check("approved plugin tool listed", any(t["id"] == "plugin:org.example.counter:counter" for t in tools))
        check("select plugin tool", octl("tool", "plugin:org.example.counter:counter").returncode == 0)
        octl("simulate", "rotate", "30")
        seen = []
        got = wait_line(plug, lambda l: (seen.append(l), l.startswith("rotate_cw") and "count=3" in l)[1])
        if not got:
            print("  plugin output:", seen)
        check("plugin receives rotation", "count=3" in got, got or "; ".join(l.strip() for l in [plug.stdout.readline() for _ in range(0)]))
        octl("simulate", "click")
        got = wait_line(plug, lambda l: l.startswith("click"))
        check("plugin receives click (reset)", "count=0" in got, got)
        plug.send_signal(signal.SIGTERM)
        plug.wait(timeout=10)
        time.sleep(0.3)
        tools = json.loads(octl("tools", "--json").stdout)
        check("plugin tools removed when it exits", not any(t["id"].startswith("plugin:org.example.counter") for t in tools))
        check("active tool falls back", not status()["activeTool"].startswith("plugin:"))
        st = status()
        check("plugin consent remembered", any(p["appId"] == "org.example.counter" and p["approved"] for p in st["plugins"]))
    else:
        print("SKIP plugin example (PyGObject missing)")

    # ---------------- HTTP API
    token_path = env["XDG_RUNTIME_DIR"] + "/opendial/http-token"
    for _ in range(30):
        if os.path.exists(token_path):
            break
        time.sleep(0.1)
    check("token file exists", os.path.exists(token_path))
    check("token file private", os.path.exists(token_path) and (os.stat(token_path).st_mode & 0o077) == 0)
    token = open(token_path).read().strip()

    def http(path, body=None, tok=token, extra=None):
        req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method="POST" if body is not None else "GET",
                                     data=None if body is None else json.dumps(body).encode())
        if tok:
            req.add_header("Authorization", "Bearer " + tok)
        for k, v in (extra or {}).items():
            req.add_header(k, v)
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, json.loads(r.read())
        except urllib.error.HTTPError as e:
            return e.code, None

    check("http: no token -> 401", http("/v1/state", tok=None)[0] == 401)
    check("http: wrong token -> 401", http("/v1/state", tok="0" * 64)[0] == 401)
    check("http: Origin -> 403", http("/v1/state", extra={"Origin": "http://evil.example"})[0] == 403)
    check("http: foreign Host -> 403", http("/v1/state", extra={"Host": "evil.example"})[0] == 403)
    code, st = http("/v1/state")
    check("http: state", code == 200 and "activeTool" in st)
    check("http: set tool", http("/v1/tool", {"id": "zoom"})[0] == 200 and status()["activeTool"] == "zoom")
    check("http: bad tool -> 400", http("/v1/tool", {"id": "nope"})[0] == 400)

    if shutil.which("node"):
        node = subprocess.Popen(["node", os.path.join(src, "plugins/examples/js/dial-events.mjs")], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        procs.append(node)
        first = wait_line(node, lambda l: l.startswith("tool:"))
        check("js: reads state", first.startswith("tool: Zoom"), first)
        second = wait_line(node, lambda l: l.startswith("tool:"))
        check("js: event stream opens", second.startswith("tool: "), second)
        octl("tool", "volume")
        line = wait_line(node, lambda l: "Volume" in l)
        check("js: tool change event", "tool: Volume" in line, line)
        octl("simulate", "rotate", "15")
        line = wait_line(node, lambda l: l.startswith("turn"))
        check("js: rotate event", line.startswith("turn +"), line)
        node.terminate()
        r = subprocess.run(["node", os.path.join(src, "plugins/examples/js/dial-events.mjs"), "tool", "scroll"], env=env,
                           capture_output=True, text=True, timeout=20)
        check("js: switch tool", r.returncode == 0 and status()["activeTool"] == "scroll", r.stdout + r.stderr)
    else:
        print("SKIP js example (node missing)")
finally:
    for p in procs:
        if p.poll() is None:
            p.kill()
    d.send_signal(signal.SIGTERM)
    try:
        d.wait(timeout=10)
    except subprocess.TimeoutExpired:
        d.kill()

sys.exit(1 if failures else 0)
