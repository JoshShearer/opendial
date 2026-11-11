// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "components"

// First-run setup: connect the Dial, check that input works, pick basics, try it.
Dialog {
    id: dlg
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, (Overlay.overlay ? Overlay.overlay.width : 640) - 48)
    height: Math.min(560, (Overlay.overlay ? Overlay.overlay.height : 560) - 48)
    closePolicy: Popup.CloseOnEscape
    padding: 24
    property int step: 0
    readonly property int steps: 4
    readonly property var dev: backend.state.device || ({})
    readonly property bool connected: dlg.dev.connected === true
    readonly property var inj: ((backend.state.capabilities || {}).injection) || ({})
    property bool sawRotate: false
    property bool sawClick: false
    onOpened: { step = 0; sawRotate = false; sawClick = false }
    onClosed: backend.completeFirstRun()

    Connections {
        target: backend
        function onGesture(g) {
            if (!dlg.opened) return
            if (g.kind === "rotate") dlg.sawRotate = true
            if (g.kind === "click") dlg.sawClick = true
        }
    }

    header: ColumnLayout {
        spacing: 4
        Item { implicitHeight: 16 }
        Label {
            Layout.leftMargin: 24
            text: [qsTr("Welcome to OpenDial"), qsTr("Connect your Surface Dial"), qsTr("Make it yours"), qsTr("Try it")][dlg.step]
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        ProgressBar { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; value: (dlg.step + 1) / dlg.steps }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: dlg.step

        // 0: welcome
        ColumnLayout {
            spacing: 14
            Icon { name: "dial"; size: 72; color: Material.accent; Layout.alignment: Qt.AlignHCenter }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Hold the Dial down to open the tool menu, turn to choose a tool and click to select it. Then turn the Dial to use the tool: volume, scrolling, zoom, undo, brightness, media and anything you configure.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("This takes about a minute. You can change everything later.")
            }
            Item { Layout.fillHeight: true }
        }

        // 1: connect
        ColumnLayout {
            spacing: 12
            RowLayout {
                Icon { name: dlg.connected ? "check" : "bluetooth"; size: 28; color: dlg.connected ? "#2e9d5b" : Material.foreground }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.weight: Font.DemiBold
                    text: dlg.connected ? qsTr("Connected to %1.").arg(dlg.dev.name || qsTr("the Surface Dial"))
                                        : qsTr("No Dial connected yet.")
                }
            }
            Label {
                visible: !dlg.connected
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("If you paired it before, press the Dial to wake it. Otherwise open Bluetooth & device to pair it now.")
            }
            Button {
                visible: !dlg.connected
                text: qsTr("Open Bluetooth & device")
                onClicked: { dlg.close(); win.go("device") }
            }
            Label {
                visible: dlg.connected && dlg.dev.hidrawNode && !dlg.dev.hidrawWritable
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#c98a12"
                text: qsTr("Haptic feedback is unavailable: the haptics node is not writable. Install the OpenDial udev rule (see the installation guide) and reconnect the Dial.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: dlg.inj.activeKeyboard === undefined || dlg.inj.activeKeyboard === "" || dlg.inj.activeKeyboard === "none"
                color: "#c98a12"
                text: qsTr("OpenDial cannot send keys or scrolling on this desktop yet. Allow it on the Integrations page.")
            }
            Button {
                visible: (dlg.inj.backends || []).some((b) => b.id === "xdg-portal-remotedesktop" && b.state === "permission-needed")
                text: qsTr("Allow input control…")
                onClicked: backend.requestPortalPermission()
            }
            Item { Layout.fillHeight: true }
        }

        // 2: basics
        ColumnLayout {
            spacing: 4
            SettingCombo {
                label: qsTr("Which hand turns the Dial?")
                path: "settings.handedness"
                defaultValue: "right"
                options: [{ value: "right", text: qsTr("Right") }, { value: "left", text: qsTr("Left") }]
            }
            SettingSwitch { label: qsTr("Haptic feedback"); description: qsTr("The Dial clicks gently as you turn it"); path: "settings.haptics.enabled"; defaultValue: true }
            SettingSlider {
                label: qsTr("Sensitivity")
                path: "settings.rotation.sensitivity"
                from: 0.25; to: 4; stepSize: 0.05; defaultValue: 1; decimals: 2; unit: "×"
            }
            SettingRow {
                label: qsTr("Start when I log in")
                Switch { checked: backend.autostartEnabled(); onToggled: backend.setAutostart(checked) }
            }
            Item { Layout.fillHeight: true }
        }

        // 3: try it
        ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Turn the Dial, then click it. Hold it down to open the menu.")
            }
            RowLayout {
                spacing: 24
                StatusPill { state: dlg.sawRotate ? "ok" : "unknown"; text: dlg.sawRotate ? qsTr("Turn ✓") : qsTr("Turn") }
                StatusPill { state: dlg.sawClick ? "ok" : "unknown"; text: dlg.sawClick ? qsTr("Click ✓") : qsTr("Click") }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("Current tool: %1").arg(backend.state.activeToolName || "—")
            }
            RowLayout {
                visible: !dlg.connected
                Label { text: qsTr("No Dial? Simulate:"); opacity: 0.7 }
                Button { text: "↻"; onClicked: backend.simulate({ rotate: 10 }) }
                Button { text: qsTr("Click"); onClicked: backend.simulate({ click: 1 }) }
                Button { text: qsTr("Hold"); onClicked: backend.simulate({ longPress: true }) }
            }
            Item { Layout.fillHeight: true }
        }
    }

    background: Rectangle {
        radius: 16
        color: Material.theme === Material.Dark ? Qt.lighter(Material.backgroundColor, 1.3) : "#ffffff"
    }

    footer: RowLayout {
        spacing: 8
        Item { implicitWidth: 16 }
        Button { text: qsTr("Skip setup"); flat: true; visible: dlg.step < dlg.steps - 1; onClicked: dlg.close() }
        Item { Layout.fillWidth: true }
        Button { text: qsTr("Back"); flat: true; enabled: dlg.step > 0; onClicked: dlg.step-- }
        Button {
            text: dlg.step === dlg.steps - 1 ? qsTr("Done") : qsTr("Next")
            highlighted: true
            onClicked: dlg.step === dlg.steps - 1 ? dlg.close() : dlg.step++
        }
        Item { implicitWidth: 16; implicitHeight: 64 }
    }
}
