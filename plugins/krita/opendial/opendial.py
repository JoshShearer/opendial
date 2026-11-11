# SPDX-License-Identifier: MIT
"""OpenDial ↔ Krita bridge.

Registers Krita-specific Dial tools with the OpenDial service over the session
D-Bus (io.github.opendial). They appear in the radial menu while Krita is
focused, after you allow "Krita" once in OpenDial → Integrations → Plugins.

Turning the Dial with one of these tools selected changes the value directly
through Krita's scripting API (no keyboard shortcuts involved):

  Brush size   turn: size ±, click: toggle eraser
  Opacity      turn: painting opacity ±5 %, click: 100 %
  Canvas zoom  turn: zoom in/out, click: fit page
  Canvas angle turn: rotate ±5°, click: reset rotation
"""
import json

from krita import Extension, Krita

try:  # Krita 5 ships PyQt5; Krita 6 PyQt6
    from PyQt5.QtCore import QObject, pyqtSlot
    from PyQt5.QtDBus import QDBusConnection, QDBusInterface, QDBusServiceWatcher
except ImportError:  # pragma: no cover
    from PyQt6.QtCore import QObject, pyqtSlot
    from PyQt6.QtDBus import QDBusConnection, QDBusInterface, QDBusServiceWatcher

SERVICE = "io.github.opendial"
PATH = "/io/github/opendial/Daemon"
IFACE = "io.github.opendial.Daemon1"
APP_ID = "org.kde.krita"

TOOLS = [
    {"id": "brush-size", "name": "Brush size", "icon": "size", "description": "Turn: brush size. Click: eraser"},
    {"id": "opacity", "name": "Opacity", "icon": "opacity", "description": "Turn: painting opacity. Click: 100 %"},
    {"id": "zoom", "name": "Canvas zoom", "icon": "zoom", "description": "Turn: zoom. Click: fit page"},
    {"id": "rotate", "name": "Canvas angle", "icon": "rotate", "description": "Turn: rotate canvas. Click: reset"},
]


def _clamp(v, lo, hi):
    return max(lo, min(hi, v))


class _Receiver(QObject):
    """Receives io.github.opendial.Daemon1.PluginEvent(appId, json)."""

    def __init__(self, bridge):
        super().__init__()
        self._bridge = bridge

    @pyqtSlot(str, str)
    def pluginEvent(self, app_id, payload):
        if app_id == APP_ID:
            try:
                self._bridge.handle(json.loads(payload))
            except Exception as e:  # never let an exception escape into Krita
                print("OpenDial:", e)


class OpenDialExtension(Extension):
    def __init__(self, parent):
        super().__init__(parent)
        self._bus = None
        self._receiver = None
        self._watcher = None

    def setup(self):
        self._bus = QDBusConnection.sessionBus()
        if not self._bus.isConnected():
            print("OpenDial: no session bus")
            return
        self._receiver = _Receiver(self)
        self._bus.connect(SERVICE, PATH, IFACE, "PluginEvent", self._receiver.pluginEvent)
        # (re-)register whenever the OpenDial service starts
        self._watcher = QDBusServiceWatcher(SERVICE, self._bus, QDBusServiceWatcher.WatchForRegistration)
        self._watcher.serviceRegistered.connect(lambda _name: self._register())
        self._register()

    def createActions(self, window):
        pass

    # ------------------------------------------------------------------ D-Bus
    def _iface(self):
        return QDBusInterface(SERVICE, PATH, IFACE, self._bus)

    def _register(self):
        reg = {"appId": APP_ID, "name": "Krita", "matchAppIds": ["krita", "org.kde.krita"],
               "tools": [dict(t, rotation={"detentDegrees": 6}) for t in TOOLS]}
        reply = self._iface().call("RegisterApp", json.dumps(reg))
        args = reply.arguments()
        if args:
            print("OpenDial:", args[0])

    def _osd(self, title, text):
        self._iface().call("PluginFeedback", APP_ID, json.dumps({"osd": {"title": title, "text": text}}))

    # ------------------------------------------------------------------ events
    def handle(self, ev):
        win = Krita.instance().activeWindow()
        view = win.activeView() if win else None
        if view is None:
            return
        tool, slot, steps = ev.get("toolId"), ev.get("slot", ""), int(ev.get("steps", 0))
        turning = slot.startswith("rotate")
        click = slot == "click"
        canvas = view.canvas()

        if tool == "brush-size":
            if turning:
                size = view.brushSize()
                # 8 % per detent feels even from 1 px to 1000 px; at least 1 px
                new = round(size * 1.08 ** steps)
                if new == round(size):
                    new = round(size) + steps
                view.setBrushSize(_clamp(new, 1, 1000))
                self._osd("Brush size", f"{view.brushSize():.0f} px")
            elif click:
                Krita.instance().action("erase_action").trigger()
                self._osd("Eraser", "toggled")
        elif tool == "opacity":
            if turning:
                view.setPaintingOpacity(_clamp(view.paintingOpacity() + 0.05 * steps, 0.0, 1.0))
            elif click:
                view.setPaintingOpacity(1.0)
            self._osd("Opacity", f"{view.paintingOpacity() * 100:.0f} %")
        elif tool == "zoom" and canvas is not None:
            if turning:
                canvas.setZoomLevel(_clamp(canvas.zoomLevel() * (1.1 ** steps), 0.01, 64.0))
            elif click:
                Krita.instance().action("zoom_to_fit").trigger()
        elif tool == "rotate" and canvas is not None:
            if turning:
                canvas.setRotation((canvas.rotation() + 5 * steps) % 360)
            elif click:
                canvas.resetRotation()
            self._osd("Canvas angle", f"{canvas.rotation():.0f}°")
