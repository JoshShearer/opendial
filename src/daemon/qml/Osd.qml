// SPDX-License-Identifier: MIT
import QtQuick

// On-screen feedback (volume level, tool switched, value changes).
Item {
    id: root
    property var osd: ({})
    property bool dark: true
    property color accent: "#4c8dff"
    property real panelOpacity: 0.94
    readonly property color fg: dark ? "#f2f4f8" : "#1b1f27"
    readonly property color fgDim: dark ? "#a8b0bf" : "#5a6272"
    readonly property bool hasValue: osd && osd.value !== undefined
    width: 320
    height: 76

    function hex(c) { return c.toString().substring(1) }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.dark ? Qt.rgba(0.09, 0.10, 0.13, root.panelOpacity) : Qt.rgba(0.97, 0.97, 0.98, root.panelOpacity)
        border.color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
    }
    Rectangle {
        id: badge
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; radius: 24
        color: root.accent
        Image {
            anchors.centerIn: parent
            width: 26; height: 26
            sourceSize: Qt.size(52, 52)
            source: "image://opendial/" + (root.osd.icon || "dial") + "?color=ffffff"
        }
    }
    Column {
        anchors.left: badge.right
        anchors.leftMargin: 14
        anchors.right: parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Row {
            width: parent.width
            Text {
                width: parent.width - valueText.width
                text: root.osd.title || ""
                color: root.fg
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                id: valueText
                text: root.hasValue ? (root.osd.text || "") : ""
                color: root.fgDim
                font.pixelSize: 14
            }
        }
        Rectangle {
            visible: root.hasValue
            width: parent.width
            height: 6
            radius: 3
            color: root.dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.10)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root.osd.value || 0))
                height: parent.height
                radius: 3
                color: root.osd.muted ? root.fgDim : root.accent
                Behavior on width { NumberAnimation { duration: 90 } }
            }
        }
        Text {
            visible: !root.hasValue && (root.osd.text || "").length > 0
            width: parent.width
            text: root.osd.text || ""
            color: root.fgDim
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }
}
