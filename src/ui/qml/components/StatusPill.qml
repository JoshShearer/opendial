// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// state: supported | permission-needed | not-supported | fallback-available | ok | warn | error
Rectangle {
    id: pill
    property string state: "supported"
    property string text: {
        switch (state) {
        case "supported": return qsTr("Supported")
        case "permission-needed": return qsTr("Permission needed")
        case "not-supported": return qsTr("Not supported")
        case "fallback-available": return qsTr("Fallback")
        default: return state
        }
    }
    readonly property color tone: (state === "supported" || state === "ok") ? "#2e9e5b"
                                 : (state === "permission-needed" || state === "fallback-available" || state === "warn") ? "#c98a12" : "#c94848"
    implicitWidth: lbl.implicitWidth + 18
    implicitHeight: 24
    radius: 12
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.16)
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.5)
    Label {
        id: lbl
        anchors.centerIn: parent
        text: pill.text
        color: pill.tone
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
}
