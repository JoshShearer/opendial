// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

ScrollView {
    id: view
    contentWidth: availableWidth
    readonly property var bt: backend.state.bluetooth || ({})
    readonly property var dev: backend.state.device || ({})
    property var nodes: []
    function refreshNodes() { const d = backend.diagnostics(true); nodes = d.nodes || [] }
    Component.onCompleted: if (backend.daemonRunning) refreshNodes()

    ColumnLayout {
        width: view.availableWidth
        spacing: 16
        Item { implicitHeight: 8 }
        Label { text: qsTr("Bluetooth & device"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.leftMargin: 24 }
        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 16

            Card {
                title: qsTr("Pair a Surface Dial")
                subtitle: qsTr("1. Click Search.  2. Remove the Dial's bottom cover and hold the button next to the batteries until the light blinks.  3. Click Pair when it appears.")
                RowLayout {
                    Button {
                        text: view.bt.discovering ? qsTr("Stop searching") : qsTr("Search")
                        highlighted: !view.bt.discovering
                        enabled: view.bt.available === true && view.bt.adapterPowered === true
                        onClicked: backend.bluetooth(view.bt.discovering ? "stop-discovery" : "discover")
                    }
                    BusyIndicator { running: view.bt.discovering === true; visible: running; implicitWidth: 32; implicitHeight: 32 }
                    Label {
                        visible: view.bt.available !== true || view.bt.adapterPowered !== true
                        text: view.bt.available !== true ? qsTr("BlueZ is not available (is bluetooth.service running?)") : qsTr("The Bluetooth adapter is off.")
                        color: "#c98a12"
                    }
                }
                Repeater {
                    model: view.bt.devices || []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Icon { name: "dial"; size: 30; color: modelData.connected ? Material.accent : Material.foreground; opacity: modelData.connected ? 1 : 0.5 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.name || qsTr("Surface Dial"); font.weight: Font.DemiBold }
                            Label {
                                text: [modelData.connected ? qsTr("connected") : qsTr("not connected"), modelData.paired ? qsTr("paired") : qsTr("not paired"),
                                       modelData.trusted ? qsTr("trusted") : qsTr("not trusted"),
                                       typeof modelData.battery === "number" ? qsTr("battery %1%").arg(modelData.battery) : ""].filter((s) => s.length).join(" · ")
                                opacity: 0.65; font.pixelSize: 12
                            }
                        }
                        Button { visible: !modelData.paired; text: qsTr("Pair"); highlighted: true; onClicked: backend.bluetooth("pair", modelData.path) }
                        Button { visible: modelData.paired && !modelData.trusted; text: qsTr("Trust"); onClicked: backend.bluetooth("trust", modelData.path); ToolTip.visible: hovered; ToolTip.text: qsTr("Lets the Dial reconnect by itself") }
                        Button { visible: modelData.paired && !modelData.connected; text: qsTr("Connect"); onClicked: backend.bluetooth("connect", modelData.path) }
                        Button { visible: modelData.connected; text: qsTr("Disconnect"); flat: true; onClicked: backend.bluetooth("disconnect", modelData.path) }
                        Button { text: qsTr("Forget"); flat: true; onClicked: backend.bluetooth("remove", modelData.path) }
                    }
                }
                Label {
                    visible: (view.bt.devices || []).length === 0
                    text: qsTr("No Surface Dial known to Bluetooth yet.")
                    opacity: 0.6
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.6
                    font.pixelSize: 12
                    text: qsTr("A paired Dial sleeps after a few minutes. Press or turn it to wake it; it reconnects in a second or two.")
                }
            }

            Card {
                title: qsTr("Device access")
                subtitle: qsTr("OpenDial reads the Dial's input node and writes haptics to its hidraw node. The installed udev rule grants this to the logged-in user.")
                GridLayout {
                    columns: 2
                    columnSpacing: 16
                    Label { text: qsTr("Input node"); opacity: 0.7 }
                    Label { text: (view.dev.evdevNode || qsTr("not found")) + (view.dev.evdevNode ? (view.dev.evdevReadable ? qsTr("  readable") : qsTr("  NOT readable")) : "") }
                    Label { text: qsTr("Haptics node"); opacity: 0.7 }
                    Label { text: (view.dev.hidrawNode || qsTr("not found")) + (view.dev.hidrawNode ? (view.dev.hidrawWritable ? qsTr("  writable") : qsTr("  NOT writable")) : "") }
                    Label { text: qsTr("Exclusive"); opacity: 0.7 }
                    Label { text: view.dev.grabbed ? qsTr("yes: other apps do not see raw Dial events") : qsTr("no") }
                }
                SettingSwitch {
                    label: qsTr("Take exclusive control")
                    description: qsTr("Prevents apps that understand the Dial themselves from reacting twice. Turn off to share it.")
                    path: "settings.device.grabDevice"
                    defaultValue: true
                }
                SettingSlider {
                    label: qsTr("Ignore turns right after connecting")
                    description: qsTr("The Dial replays turns made while asleep; this drops them.")
                    path: "settings.device.connectGuardMs"
                    from: 0; to: 3000; stepSize: 100; defaultValue: 800; unit: " ms"
                }
                RowLayout {
                    Button { text: qsTr("Reconnect"); enabled: backend.daemonRunning; onClicked: backend.reconnect() }
                    Button { text: qsTr("Rescan nodes"); enabled: backend.daemonRunning; onClicked: view.refreshNodes() }
                }
                Repeater {
                    model: view.nodes
                    delegate: Label {
                        required property var modelData
                        text: modelData.node + "  " + (modelData.hidName || modelData.subsystem || "") + "  " + (modelData.readable ? "r" : "-") + (modelData.writable ? "w" : "-")
                        font.family: "monospace"
                        font.pixelSize: 12
                        opacity: 0.8
                    }
                }
            }
        }
        Item { implicitHeight: 24 }
    }
}
