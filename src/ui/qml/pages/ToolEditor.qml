// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Drawer {
    id: drawer
    property string path: ""
    readonly property var tool: { backend.config; return path ? (backend.get(path) || ({})) : ({}) }
    readonly property var rot: tool.rotation || ({})
    edge: Qt.RightEdge
    width: Math.min(640, win.width * 0.62)
    height: win.height
    interactive: true

    readonly property var slots: [
        { id: "rotate_cw", name: qsTr("Turn right (clockwise)"), sym: "↻" },
        { id: "rotate_ccw", name: qsTr("Turn left (counter-clockwise)"), sym: "↺" },
        { id: "click", name: qsTr("Click"), sym: "●" },
        { id: "double_click", name: qsTr("Double-click"), sym: "●●" },
        { id: "triple_click", name: qsTr("Triple-click"), sym: "●●●" },
        { id: "long_press", name: qsTr("Press and hold"), sym: "◉" },
        { id: "rotate_while_pressed_cw", name: qsTr("Press and turn right"), sym: "◉↻" },
        { id: "rotate_while_pressed_ccw", name: qsTr("Press and turn left"), sym: "◉↺" },
        { id: "release", name: qsTr("Release after hold"), sym: "○" }
    ]

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: drawer.width - 8
            spacing: 14
            Item { implicitHeight: 6 }
            RowLayout {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                ToolButton {
                    id: iconBtn
                    implicitWidth: 56; implicitHeight: 56
                    icon.source: "image://opendial/" + (drawer.tool.icon || "custom") + "?color=" + Material.accent.toString().substring(1)
                    icon.width: 36; icon.height: 36
                    onClicked: iconPicker.open()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Choose icon")
                    Accessible.name: qsTr("Choose icon")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        font.pixelSize: 20
                        text: drawer.tool.name || ""
                        placeholderText: qsTr("Tool name")
                        onEditingFinished: backend.set(drawer.path + ".name", text)
                        Accessible.name: qsTr("Tool name")
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: drawer.tool.description || ""
                        placeholderText: qsTr("Description (optional)")
                        onEditingFinished: backend.set(drawer.path + ".description", text)
                    }
                }
                ToolButton { text: "✕"; onClicked: drawer.close(); Accessible.name: qsTr("Close") }
            }

            Card {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Turning")
                SettingCombo {
                    label: qsTr("Mode")
                    path: drawer.path + ".rotation.mode"
                    defaultValue: backend.get("settings.rotation.mode") || "stepped"
                    options: [{ value: "stepped", text: qsTr("Steps (one action per detent)") }, { value: "continuous", text: qsTr("Continuous (fine)") }]
                }
                SettingSlider {
                    label: qsTr("Turn per step")
                    description: qsTr("How far to turn for one action. The Dial clicks at each step when haptics are on.")
                    path: drawer.path + ".rotation.detentDegrees"
                    from: 2; to: 90; stepSize: 1; unit: "°"
                    defaultValue: backend.get("settings.rotation.detentDegrees") || 10
                }
                SettingSlider {
                    label: qsTr("Sensitivity")
                    path: drawer.path + ".rotation.sensitivity"
                    from: 0.25; to: 4; stepSize: 0.05; decimals: 2; unit: "×"
                    defaultValue: backend.get("settings.rotation.sensitivity") || 1
                }
                SettingSlider {
                    label: qsTr("Acceleration")
                    description: qsTr("Fast spins do more. Disables detent haptics.")
                    path: drawer.path + ".rotation.acceleration"
                    from: 0; to: 3; stepSize: 0.1; decimals: 1
                    defaultValue: 0
                }
                SettingSwitch { label: qsTr("Reverse direction"); path: drawer.path + ".rotation.invert"; defaultValue: false }
                SettingSwitch { label: qsTr("Haptic clicks"); description: qsTr("Feel a click at every step"); path: drawer.path + ".haptics"; defaultValue: true }
            }

            Card {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("What the Dial does")
                subtitle: qsTr("Each gesture can do something different. Unused gestures do nothing.")
                Repeater {
                    model: drawer.slots
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        readonly property var binding: (drawer.tool.bindings || {})[modelData.id]
                        readonly property bool menuGesture: modelData.id === "long_press" && (backend.get("settings.menu.openGesture") || "long_press") === "long_press"
                            || modelData.id === "double_click" && backend.get("settings.menu.openGesture") === "double_click"
                            || modelData.id === "triple_click" && backend.get("settings.menu.openGesture") === "triple_click"
                        property bool expanded: binding !== undefined
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.sym; Layout.preferredWidth: 34; font.pixelSize: 16; horizontalAlignment: Text.AlignHCenter }
                            Label { text: modelData.name; Layout.fillWidth: true; font.weight: Font.DemiBold }
                            Label {
                                visible: !parent.parent.expanded || parent.parent.menuGesture
                                text: parent.parent.menuGesture ? qsTr("opens the radial menu") : qsTr("nothing")
                                opacity: 0.55
                            }
                            Button {
                                visible: parent.parent.binding === undefined && !parent.parent.menuGesture
                                text: qsTr("Set")
                                flat: true
                                onClicked: backend.set(drawer.path + ".bindings." + modelData.id, { type: "key", keys: "" })
                            }
                            ToolButton {
                                visible: parent.parent.binding !== undefined
                                icon.source: "image://opendial/delete?color=" + Material.foreground.toString().substring(1)
                                onClicked: backend.remove(drawer.path + ".bindings." + modelData.id)
                                Accessible.name: qsTr("Clear %1").arg(modelData.name)
                            }
                        }
                        Loader {
                            Layout.fillWidth: true
                            Layout.leftMargin: 34
                            active: parent.binding !== undefined && !parent.menuGesture
                            sourceComponent: ActionEditor { path: drawer.path + ".bindings." + modelData.id }
                        }
                        MenuSeparator { Layout.fillWidth: true }
                    }
                }
            }

            Card {
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Try it")
                subtitle: qsTr("Makes this the active tool and sends a simulated gesture. The action goes to whatever window is focused.")
                RowLayout {
                    enabled: backend.daemonRunning
                    Button { text: "↺ " + qsTr("Left"); onClicked: { backend.setActiveTool(drawer.tool.id); backend.simulate({ rotate: -((drawer.rot.detentDegrees) || 10) }) } }
                    Button { text: qsTr("Right") + " ↻"; onClicked: { backend.setActiveTool(drawer.tool.id); backend.simulate({ rotate: (drawer.rot.detentDegrees || 10) }) } }
                    Button { text: "● " + qsTr("Click"); onClicked: { backend.setActiveTool(drawer.tool.id); backend.simulate({ click: 1 }) } }
                }
            }
            Item { implicitHeight: 20 }
        }
    }

    Popup {
        id: iconPicker
        x: 20; y: 80
        width: drawer.width - 40
        height: 340
        modal: true
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                Label { text: qsTr("Choose an icon"); font.weight: Font.DemiBold; Layout.fillWidth: true }
                TextField {
                    id: customIcon
                    placeholderText: qsTr("or path to .svg / .png")
                    Layout.preferredWidth: 240
                    onAccepted: { backend.set(drawer.path + ".icon", text); iconPicker.close() }
                }
            }
            GridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 52; cellHeight: 52
                model: backend.icons
                delegate: ToolButton {
                    required property string modelData
                    width: 48; height: 48
                    icon.source: "image://opendial/" + modelData + "?color=" + Material.foreground.toString().substring(1)
                    icon.width: 26; icon.height: 26
                    highlighted: drawer.tool.icon === modelData
                    onClicked: { backend.set(drawer.path + ".icon", modelData); iconPicker.close() }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData
                    Accessible.name: modelData
                }
            }
        }
    }
}
