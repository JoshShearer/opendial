// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

SettingRow {
    id: r
    property string path: ""
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real defaultValue: 0
    property string unit: ""
    property int decimals: 0
    readonly property var raw: { backend.config; return backend.get(path) }
    RowLayout {
        spacing: 8
        Slider {
            id: s
            implicitWidth: 220
            from: r.from
            to: r.to
            stepSize: r.stepSize
            snapMode: Slider.SnapAlways
            value: r.raw === undefined || r.raw === null ? r.defaultValue : r.raw
            onMoved: backend.set(r.path, r.decimals > 0 ? Number(value.toFixed(r.decimals)) : Math.round(value))
            Accessible.name: r.label
        }
        Label {
            text: s.value.toFixed(r.decimals) + r.unit
            Layout.preferredWidth: 64
            horizontalAlignment: Text.AlignRight
        }
    }
}
