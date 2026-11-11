#!/usr/bin/env python3
# Minimal org.freedesktop.login1 stand-in so GNOME Shell starts in a container
# without systemd. Test scaffolding only: it answers the few calls the Shell
# makes at startup and nothing else.
from gi.repository import Gio, GLib

XML = '''<node>
 <interface name="org.freedesktop.login1.Manager">
  <method name="GetSession"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
  <method name="GetSessionByPID"><arg type="u" direction="in"/><arg type="o" direction="out"/></method>
  <method name="ListSessions"><arg type="a(susso)" direction="out"/></method>
  <method name="Inhibit"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="h" direction="out"/></method>
  <method name="CanSuspend"><arg type="s" direction="out"/></method>
  <method name="CanRebootToBootLoaderMenu"><arg type="s" direction="out"/></method>
  <signal name="PrepareForSleep"><arg type="b"/></signal>
  <property name="IdleHint" type="b" access="read"/>
 </interface>
 <interface name="org.freedesktop.login1.Session">
  <property name="Active" type="b" access="read"/>
  <property name="Id" type="s" access="read"/>
  <property name="Class" type="s" access="read"/>
  <property name="Type" type="s" access="read"/>
  <property name="Remote" type="b" access="read"/>
  <property name="LockedHint" type="b" access="read"/>
  <method name="SetLockedHint"><arg type="b" direction="in"/></method>
  <method name="TakeControl"><arg type="b" direction="in"/></method>
  <signal name="Lock"/><signal name="Unlock"/>
 </interface>
</node>'''
SESSION = '/org/freedesktop/login1/session/c1'

def call(conn, sender, path, iface, method, params, inv):
    if method in ('GetSession', 'GetSessionByPID'):
        inv.return_value(GLib.Variant('(o)', (SESSION,)))
    elif method == 'ListSessions':
        inv.return_value(GLib.Variant('(a(susso))', ([('c1', 0, 'root', 'seat0', SESSION)],)))
    elif method in ('CanSuspend', 'CanRebootToBootLoaderMenu'):
        inv.return_value(GLib.Variant('(s)', ('na',)))
    elif method in ('SetLockedHint', 'TakeControl'):
        inv.return_value(None)
    else:
        inv.return_dbus_error('org.freedesktop.DBus.Error.NotSupported', method)

def prop(conn, sender, path, iface, name):
    return {'Active': GLib.Variant('b', True), 'Id': GLib.Variant('s', 'c1'), 'Class': GLib.Variant('s', 'user'),
            'Type': GLib.Variant('s', 'wayland'), 'Remote': GLib.Variant('b', False), 'LockedHint': GLib.Variant('b', False),
            'IdleHint': GLib.Variant('b', False)}.get(name)

info = Gio.DBusNodeInfo.new_for_xml(XML)
bus = Gio.bus_get_sync(Gio.BusType.SYSTEM)
bus.register_object('/org/freedesktop/login1', info.interfaces[0], call, prop, None)
bus.register_object(SESSION, info.interfaces[1], call, prop, None)
Gio.bus_own_name_on_connection(bus, 'org.freedesktop.login1', 0, None, None)
GLib.MainLoop().run()
