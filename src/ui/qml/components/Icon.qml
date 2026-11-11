// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material

// Tinted tool icon from the shared image provider (image://opendial/<name>?color=).
Item {
    id: icon
    property string name: "custom"
    property color color: Material.foreground
    property int size: 24
    implicitWidth: size
    implicitHeight: size
    Image {
        anchors.fill: parent
        sourceSize: Qt.size(icon.size * 2, icon.size * 2)
        source: "image://opendial/" + (icon.name || "custom") + "?color=" + icon.color.toString().substring(1)
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: false
    }
}
