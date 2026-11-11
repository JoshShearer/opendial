// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// label + description on the left, control on the right
RowLayout {
    id: row
    property string label: ""
    property string description: ""
    default property alias control: holder.data
    Layout.fillWidth: true
    spacing: 16
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 1
        Label { text: row.label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Label {
            visible: row.description.length > 0
            text: row.description
            opacity: 0.65
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    Item {
        id: holder
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
    }
}
