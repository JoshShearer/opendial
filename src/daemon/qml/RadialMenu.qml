// SPDX-License-Identifier: MIT
import QtQuick

// Radial tool menu. Driven entirely by the daemon: `menu` is the JSON from
// Engine::menuJson(). Not interactive (input-transparent): the Dial is the
// only input device.
Item {
    id: root
    property var menu: ({ items: [], highlight: 0 })
    property bool dark: true
    property color accent: "#4c8dff"
    property real panelOpacity: 0.94
    property bool reduceMotion: false
    property bool showHints: true
    property real menuSize: 360

    readonly property var items: menu && menu.items ? menu.items : []
    readonly property int count: items.length
    readonly property int highlight: menu ? (menu.highlight || 0) : 0
    readonly property var current: count > 0 && highlight < count ? items[highlight] : null
    readonly property real radius: menuSize / 2
    readonly property real ringRadius: radius * 0.68
    readonly property real itemSize: Math.min(radius * 0.42, 2 * Math.PI * ringRadius / Math.max(count, 1) * 0.78)
    readonly property color fg: dark ? "#f2f4f8" : "#1b1f27"
    readonly property color fgDim: dark ? "#a8b0bf" : "#5a6272"
    readonly property color panel: dark ? Qt.rgba(0.09, 0.10, 0.13, panelOpacity) : Qt.rgba(0.97, 0.97, 0.98, panelOpacity)
    readonly property color ring: dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.06)

    width: menuSize
    height: menuSize

    function angleFor(i) { return -Math.PI / 2 + (2 * Math.PI * i) / Math.max(count, 1) }
    function hex(c) { return c.toString().substring(1) }

    // disc
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.panel
        border.color: root.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.10)
        border.width: 1
    }
    // item track
    Rectangle {
        anchors.centerIn: parent
        width: root.ringRadius * 2 + root.itemSize
        height: width
        radius: width / 2
        color: "transparent"
        border.color: root.ring
        border.width: root.itemSize
        opacity: 0.6
    }

    // highlight wedge, rotates to the selected item
    Item {
        id: wedge
        anchors.centerIn: parent
        width: root.menuSize
        height: root.menuSize
        visible: root.count > 0
        rotation: root.count > 0 ? (360 * root.highlight / root.count) : 0
        Behavior on rotation {
            enabled: !root.reduceMotion
            RotationAnimation { duration: 120; direction: RotationAnimation.Shortest; easing.type: Easing.OutCubic }
        }
        Rectangle {
            width: root.itemSize * 1.22
            height: width
            radius: width / 2
            x: wedge.width / 2 - width / 2
            y: wedge.height / 2 - root.ringRadius - height / 2
            color: root.accent
            opacity: 0.95
        }
    }

    Repeater {
        model: root.items
        delegate: Item {
            required property var modelData
            required property int index
            readonly property bool hl: index === root.highlight
            width: root.itemSize
            height: root.itemSize
            x: root.radius + root.ringRadius * Math.cos(root.angleFor(index)) - width / 2
            y: root.radius + root.ringRadius * Math.sin(root.angleFor(index)) - height / 2
            scale: hl ? 1.08 : 1.0
            Behavior on scale { enabled: !root.reduceMotion; NumberAnimation { duration: 100 } }

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: modelData.active && !parent.hl ? 2 : 0
                border.color: root.accent
            }
            Image {
                anchors.centerIn: parent
                width: parent.width * 0.5
                height: width
                sourceSize: Qt.size(width * 2, height * 2)
                source: "image://opendial/" + (modelData.icon || "custom") + "?color=" + root.hex(parent.hl ? "#ffffff" : root.fg)
                smooth: true
            }
            Rectangle { // folder marker
                visible: modelData.folder === true
                width: 6; height: 6; radius: 3
                color: parent.hl ? "#ffffff" : root.fgDim
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: parent.height * 0.08
            }
        }
    }

    // centre label
    Column {
        anchors.centerIn: parent
        width: root.ringRadius * 1.25
        spacing: 4
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.menu && root.menu.breadcrumb ? root.menu.breadcrumb : ""
            visible: text.length > 0
            color: root.fgDim
            font.pixelSize: Math.max(10, root.menuSize * 0.032)
            elide: Text.ElideMiddle
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.current ? root.current.name : ""
            color: root.fg
            font.pixelSize: Math.max(13, root.menuSize * 0.055)
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.showHints && root.current ? root.current.hint : ""
            visible: text.length > 0
            color: root.fgDim
            font.pixelSize: Math.max(10, root.menuSize * 0.033)
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 5
            visible: root.menu && root.menu.pageCount > 1
            Repeater {
                model: root.menu && root.menu.pageCount ? root.menu.pageCount : 0
                Rectangle {
                    required property int index
                    width: 6; height: 6; radius: 3
                    color: index === root.menu.page ? root.accent : root.fgDim
                    opacity: index === root.menu.page ? 1 : 0.5
                }
            }
        }
    }
}
