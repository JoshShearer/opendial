// SPDX-License-Identifier: MIT
import QtQuick

// On-screen feedback (volume level, tool switched, value changes).
Item {
    id: root
    property var osd: ({})
    property bool dark: true
    property color background: "#171a21"
    property color foreground: "#f2f4f8"
    property color foregroundDim: Qt.tint(foreground, Qt.rgba(background.r, background.g, background.b, 0.38))
    property color accent: "#4c8dff"
    property color accentForeground: "#ffffff"
    property int cornerRadius: 12   // follows the compositor's window rounding (0 on Omarchy)
    property real panelOpacity: 0.94
    readonly property color fg: foreground
    readonly property color fgDim: foregroundDim
    readonly property bool hasValue: osd && osd.value !== undefined
    width: 320
    height: 76

    function hex(c) { return c.toString().substring(1) }

    Rectangle {
        anchors.fill: parent
        radius: Math.min(height / 2, root.cornerRadius)
        color: Qt.rgba(root.background.r, root.background.g, root.background.b, root.panelOpacity)
        border.color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, 0.10)
    }
    Rectangle {
        id: badge
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        width: 48; height: 48; radius: Math.min(24, root.cornerRadius)
        color: root.accent
        Image {
            anchors.centerIn: parent
            width: 26; height: 26
            sourceSize: Qt.size(52, 52)
            source: "image://opendial/" + (root.osd.icon || "dial") + "?color=" + root.hex(root.accentForeground)
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
            radius: Math.min(3, root.cornerRadius)
            color: Qt.rgba(root.foreground.r, root.foreground.g, root.foreground.b, root.dark ? 0.12 : 0.10)
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, root.osd.value || 0))
                height: parent.height
                radius: parent.radius
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
