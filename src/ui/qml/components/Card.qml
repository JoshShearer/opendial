// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: card
    property string title: ""
    property string subtitle: ""
    default property alias content: body.data
    Layout.fillWidth: true
    Layout.alignment: Qt.AlignTop
    padding: 18
    Material.elevation: 1
    background: Rectangle {
        radius: 12
        color: Material.theme === Material.Dark ? Qt.lighter(Material.backgroundColor, 1.25) : "#ffffff"
        border.color: Material.theme === Material.Dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.08)
    }
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10
        ColumnLayout {
            visible: card.title.length > 0
            spacing: 2
            Label { text: card.title; font.pixelSize: 17; font.weight: Font.DemiBold }
            Label {
                visible: card.subtitle.length > 0
                text: card.subtitle
                opacity: 0.7
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 10
        }
    }
}
