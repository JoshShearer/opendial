// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// options: [{value: "x", text: "X"}, ...]
SettingRow {
    id: r
    property string path: ""
    property var options: []
    property string defaultValue: ""
    readonly property var raw: { backend.config; return backend.get(path) }
    ComboBox {
        implicitWidth: 240
        model: r.options
        textRole: "text"
        valueRole: "value"
        currentIndex: {
            const v = (r.raw === undefined || r.raw === null) ? r.defaultValue : r.raw
            for (let i = 0; i < r.options.length; ++i)
                if (r.options[i].value === v) return i
            return 0
        }
        onActivated: backend.set(r.path, currentValue)
        Accessible.name: r.label
    }
}
