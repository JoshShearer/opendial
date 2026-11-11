// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

SettingRow {
    id: r
    property string path: ""
    property bool defaultValue: false
    readonly property var raw: { backend.config; return backend.get(path) }
    Switch {
        checked: r.raw === undefined || r.raw === null ? r.defaultValue : r.raw
        onToggled: backend.set(r.path, checked)
        Accessible.name: r.label
    }
}
