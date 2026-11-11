// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

ScrollView {
    id: view
    contentWidth: availableWidth
    readonly property var st: backend.state
    readonly property var dev: st.device || ({})
    readonly property var caps: st.capabilities || ({})
    readonly property var hap: st.haptics || ({})

    ColumnLayout {
        width: view.availableWidth
        spacing: 16
        Item { implicitHeight: 8 }
        Label { text: qsTr("Dashboard"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.leftMargin: 24 }

        GridLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            columns: view.availableWidth > 900 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16

            Card {
                title: qsTr("Surface Dial")
                RowLayout {
                    spacing: 16
                    Rectangle {
                        width: 72; height: 72; radius: 36
                        color: view.dev.connected ? Material.accent : Qt.rgba(0.5, 0.5, 0.5, 0.25)
                        Icon { anchors.centerIn: parent; name: "dial"; size: 40; color: "white" }
                    }
                    ColumnLayout {
                        Label { text: view.dev.connected ? qsTr("Connected") : qsTr("Not connected"); font.pixelSize: 18; font.weight: Font.DemiBold }
                        Label {
                            text: view.dev.connected
                                  ? (typeof view.dev.battery === "number" ? qsTr("Battery %1%").arg(view.dev.battery) : qsTr("Battery level not reported"))
                                  : qsTr("Press or turn the Dial to wake it. Pair it under Bluetooth & device.")
                            opacity: 0.75
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: view.hap.available ? qsTr("Haptics ready") : qsTr("Haptics unavailable: %1").arg(view.hap.error || "")
                            opacity: 0.75
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
                RowLayout {
                    Button { text: qsTr("Test haptics"); enabled: view.hap.available === true; onClicked: backend.testHaptics("click") }
                    Button { text: qsTr("Show radial menu"); enabled: backend.daemonRunning; onClicked: backend.openMenu() }
                    Button { text: view.st.paused ? qsTr("Resume") : qsTr("Pause"); enabled: backend.daemonRunning; onClicked: backend.setPaused(!view.st.paused) }
                }
            }

            Card {
                title: qsTr("Now")
                RowLayout {
                    spacing: 14
                    Icon { name: view.st.activeToolIcon || "custom"; size: 40; color: Material.accent }
                    ColumnLayout {
                        Label { text: view.st.activeToolName || qsTr("No tool"); font.pixelSize: 18; font.weight: Font.DemiBold }
                        Label {
                            text: qsTr("Profile: %1").arg(view.st.profileName || "—") + (view.st.lockedProfile ? qsTr(" (locked)") : "")
                            opacity: 0.75
                        }
                        Label {
                            readonly property var f: view.st.focus || ({})
                            text: f.appId ? qsTr("Focused app: %1").arg(f.appId) : qsTr("Focused app unknown")
                            opacity: 0.75
                        }
                        Label {
                            text: (view.st.profile && view.st.profile.reason) ? qsTr("Why: %1").arg(view.st.profile.reason) : ""
                            opacity: 0.6
                            font.pixelSize: 12
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: view.st.menuTools || []
                        delegate: Button {
                            required property var modelData
                            visible: modelData.kind !== "folder"
                            text: modelData.name
                            checkable: true
                            checked: modelData.id === view.st.activeTool
                            onClicked: backend.setActiveTool(modelData.id)
                            icon.source: "image://opendial/" + (modelData.icon || "custom") + "?color=" + Material.foreground.toString().substring(1)
                            flat: !checked
                        }
                    }
                }
                RowLayout {
                    Button {
                        text: view.st.lockedProfile ? qsTr("Unlock profile") : qsTr("Lock this profile")
                        enabled: backend.daemonRunning
                        onClicked: view.st.lockedProfile ? backend.unlockProfile() : backend.lockProfile(view.st.profile.profileId)
                    }
                }
            }

            Card {
                title: qsTr("What works in this session")
                subtitle: qsTr("OpenDial adapts to your desktop. Items that need attention link to the fix.")
                Repeater {
                    model: [
                        { name: qsTr("Keyboard shortcuts"), state: (view.caps.injection || {}).keyboard, detail: (view.caps.injection || {}).activeKeyboard, page: "integrations" },
                        { name: qsTr("Scrolling and clicks"), state: (view.caps.injection || {}).pointer, detail: (view.caps.injection || {}).activePointer, page: "integrations" },
                        { name: qsTr("App profiles (focused window)"), state: (view.caps.context || {}).active ? "supported" : "not-supported", detail: (view.caps.context || {}).active, page: "integrations" },
                        { name: qsTr("Volume"), state: (view.caps.audio || {}).state, detail: (view.caps.audio || {}).backend, page: "integrations" },
                        { name: qsTr("Media players (MPRIS)"), state: (view.caps.media || {}).state, detail: ((view.caps.media || {}).players || []).length + qsTr(" player(s)"), page: "integrations" },
                        { name: qsTr("Screen brightness"), state: (view.caps.brightness || {}).state, detail: (view.caps.brightness || {}).detail, page: "integrations" },
                        { name: qsTr("Workspaces"), state: (view.caps.workspace || {}).state, detail: (view.caps.workspace || {}).method, page: "integrations" }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.name; Layout.fillWidth: true }
                        Label { text: modelData.detail || ""; opacity: 0.6; font.pixelSize: 12 }
                        StatusPill { state: modelData.state || "not-supported" }
                        ToolButton {
                            visible: modelData.state !== "supported"
                            text: qsTr("Fix")
                            onClicked: win.go(modelData.page)
                        }
                    }
                }
            }

            Card {
                title: qsTr("Last activity")
                Label {
                    text: view.st.lastGesture && view.st.lastGesture.kind ? qsTr("Gesture: %1").arg(view.st.lastGesture.kind) : qsTr("No input yet")
                }
                Label { text: view.st.lastAction ? qsTr("Action: %1").arg(view.st.lastAction) : ""; opacity: 0.75; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label {
                    visible: (view.st.pendingApprovals || 0) > 0
                    text: qsTr("%1 action(s) are waiting for your approval.").arg(view.st.pendingApprovals)
                    color: "#c98a12"
                }
                Button { visible: (view.st.pendingApprovals || 0) > 0; text: qsTr("Review"); onClicked: win.go("integrations") }
            }
        }
        Item { implicitHeight: 24 }
    }
}
