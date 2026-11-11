// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

ScrollView {
    id: view
    contentWidth: availableWidth
    property bool autostart: backend.autostartEnabled()

    ColumnLayout {
        width: view.availableWidth
        spacing: 16
        Item { implicitHeight: 8 }
        Label { text: qsTr("Settings"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.leftMargin: 24 }
        GridLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            columns: view.availableWidth > 900 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16

            Card {
                title: qsTr("General")
                SettingRow {
                    label: qsTr("Start OpenDial when I log in")
                    Switch { checked: view.autostart; onToggled: { backend.setAutostart(checked); view.autostart = backend.autostartEnabled() } }
                }
                SettingCombo {
                    label: qsTr("Handedness")
                    description: qsTr("Places the menu beside the pointer, away from the hand on the Dial (when “beside” is chosen)")
                    path: "settings.handedness"
                    defaultValue: "right"
                    options: [{ value: "right", text: qsTr("Right-handed") }, { value: "left", text: qsTr("Left-handed") }]
                }
                SettingCombo {
                    label: qsTr("Menu position at the pointer")
                    path: "settings.menu.pointerOffset"
                    defaultValue: "center"
                    options: [{ value: "center", text: qsTr("Centred on the pointer") }, { value: "beside", text: qsTr("Beside the pointer") }]
                }
                SettingSwitch { label: qsTr("Tray icon"); path: "settings.ui.tray"; defaultValue: true }
            }

            Card {
                title: qsTr("On-screen feedback")
                SettingSwitch { label: qsTr("Show volume, brightness and tool changes"); path: "settings.osd.enabled"; defaultValue: true }
                SettingCombo {
                    label: qsTr("Position")
                    path: "settings.osd.position"
                    defaultValue: "bottom"
                    options: [{ value: "bottom", text: qsTr("Bottom") }, { value: "center", text: qsTr("Centre") }, { value: "top", text: qsTr("Top") }]
                }
                SettingSlider { label: qsTr("Duration"); path: "settings.osd.durationMs"; from: 400; to: 5000; stepSize: 100; defaultValue: 1200; unit: " ms" }
                SettingSwitch { label: qsTr("Notifications"); description: qsTr("Connect, low battery, approvals"); path: "settings.notifications.enabled"; defaultValue: true }
                SettingSwitch { label: qsTr("Only important notifications"); path: "settings.notifications.quiet"; defaultValue: false }
            }

            Card {
                title: qsTr("Privacy")
                SettingSwitch {
                    label: qsTr("Redact diagnostics")
                    description: qsTr("Remove Bluetooth addresses, window titles and paths from exported reports")
                    path: "settings.privacy.redactDiagnostics"
                    defaultValue: true
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("OpenDial never sends data anywhere. Window titles are only used for window-specific profiles and are never written to disk.")
                }
            }

            Card {
                title: qsTr("Configuration")
                Label { text: backend.state.configPath || ""; font.family: "monospace"; font.pixelSize: 12; opacity: 0.8; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                Repeater {
                    model: backend.warnings
                    delegate: Label { required property string modelData; text: "⚠ " + modelData; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#c98a12" }
                }
                RowLayout {
                    Button { text: qsTr("Undo last change"); enabled: backend.daemonRunning; onClicked: backend.rollback() }
                    Button { text: qsTr("Restore defaults…"); onClicked: confirmReset.open() }
                    Button { text: qsTr("Run setup again"); onClicked: wizard.open() }
                }
            }

            Card {
                title: qsTr("About")
                Label { text: "OpenDial " + backend.version; font.weight: Font.DemiBold }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.75
                    text: qsTr("Surface Dial support for Linux. Not affiliated with Microsoft. “Surface” is a trademark of Microsoft Corporation.")
                }
            }
        }
        Item { implicitHeight: 24 }
    }
    Dialog {
        id: confirmReset
        title: qsTr("Restore defaults?")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("All tools, profiles and settings return to the defaults. The current file is kept as a backup."); wrapMode: Text.WordWrap; width: 380 }
        onAccepted: backend.resetDefaults()
    }
}
