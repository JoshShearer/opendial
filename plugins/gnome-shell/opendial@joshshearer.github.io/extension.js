// SPDX-License-Identifier: MIT
// OpenDial focus bridge for GNOME Shell 45+.
//
// GNOME's Wayland session does not let regular clients see the focused window
// or the pointer position. This extension reports both to the local OpenDial
// service (io.github.joshshearer.opendial on the session bus). Nothing leaves the machine.
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const SERVICE = 'io.github.joshshearer.opendial';
const PATH = '/io/github/opendial/Daemon';
const IFACE = 'io.github.joshshearer.opendial.Daemon1';

const SHELL_IFACE_XML = `
<node>
  <interface name="io.github.joshshearer.opendial.Shell">
    <method name="GetPointer">
      <arg type="i" direction="out" name="x"/>
      <arg type="i" direction="out" name="y"/>
    </method>
  </interface>
</node>`;

export default class OpenDialExtension extends Extension {
    enable() {
        this._tracker = Shell.WindowTracker.get_default();
        this._focusId = global.display.connect('notify::focus-window', () => this._queueReport());
        this._wsId = global.workspace_manager.connect('active-workspace-changed', () => this._queueReport());
        this._exported = Gio.DBusExportedObject.wrapJSObject(SHELL_IFACE_XML, this);
        this._exported.export(Gio.DBus.session, '/io/github/opendial/Shell');
        // re-report when the service (re)starts
        this._watchId = Gio.bus_watch_name(Gio.BusType.SESSION, SERVICE, Gio.BusNameWatcherFlags.NONE,
            () => this._queueReport(), null);
        this._queueReport();
    }

    disable() {
        if (this._focusId)
            global.display.disconnect(this._focusId);
        if (this._wsId)
            global.workspace_manager.disconnect(this._wsId);
        if (this._watchId)
            Gio.bus_unwatch_name(this._watchId);
        if (this._idle)
            GLib.source_remove(this._idle);
        this._exported?.unexport();
        this._exported = null;
        this._focusId = this._wsId = this._watchId = this._idle = 0;
        this._tracker = null;
    }

    GetPointer() {
        const [x, y] = global.get_pointer();
        return [x, y];
    }

    _queueReport() {
        if (this._idle)
            return;
        // coalesce bursts of focus changes into one report
        this._idle = GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
            this._idle = 0;
            this._report();
            return GLib.SOURCE_REMOVE;
        });
    }

    _report() {
        const w = global.display.focus_window;
        const p = {source: 'gnome-shell'};
        if (w) {
            const app = this._tracker.get_window_app(w);
            // Windows without a matching .desktop file get a placeholder app
            // ("window:N") that no profile can match: use the window's own
            // application ID or class instead.
            let appId = app && !app.is_window_backed() ? (app.get_id() ?? '') : '';
            if (appId.endsWith('.desktop'))
                appId = appId.slice(0, -'.desktop'.length);
            p.appId = appId || w.get_sandboxed_app_id?.() || w.get_gtk_application_id?.() || w.get_wm_class() || '';
            p.windowClass = w.get_wm_class() ?? '';
            p.title = w.get_title() ?? '';
            p.pid = w.get_pid();
        }
        const ws = global.workspace_manager.get_active_workspace();
        if (ws)
            p.workspace = String(ws.index() + 1);
        const [x, y] = global.get_pointer();
        p.pointer = {x, y};
        Gio.DBus.session.call(SERVICE, PATH, IFACE, 'ReportFocus',
            new GLib.Variant('(s)', [JSON.stringify(p)]), null,
            Gio.DBusCallFlags.NO_AUTO_START, 1000, null, () => {});
    }
}
