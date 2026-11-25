// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

ScrollView {
    id: view
    contentWidth: availableWidth
    readonly property var caps: backend.state.capabilities || ({})
    readonly property var inj: caps.injection || ({})
    readonly property var ctx: caps.context || ({})
    property var approvalList: []
    function reloadApprovals() { approvalList = backend.daemonRunning ? backend.approvals() : [] }
    Component.onCompleted: reloadApprovals()
    Connections { target: backend; function onStateChanged() { if ((backend.state.pendingApprovals || 0) !== view.lastPending) { view.lastPending = backend.state.pendingApprovals || 0; view.reloadApprovals() } } }
    property int lastPending: 0

    ColumnLayout {
        width: view.availableWidth
        spacing: 16
        Item { implicitHeight: 8 }
        Label { text: qsTr("Integrations"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.leftMargin: 24 }

        ColumnLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 16

            Card {
                title: qsTr("Sending keys, scrolling and clicks")
                subtitle: qsTr("OpenDial uses the first method your desktop supports. Wayland desktops require one of them to be allowed.")
                Repeater {
                    model: view.inj.backends || []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.label + (view.inj.activeKeyboard === modelData.id ? qsTr("  (in use)") : ""); font.weight: view.inj.activeKeyboard === modelData.id ? Font.DemiBold : Font.Normal }
                            Label { text: modelData.detail || ""; opacity: 0.6; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        StatusPill { state: modelData.state }
                        Button {
                            visible: modelData.id === "xdg-portal-remotedesktop" && modelData.state === "permission-needed"
                            text: qsTr("Allow…")
                            onClicked: backend.requestPortalPermission()
                        }
                    }
                }
                SettingCombo {
                    label: qsTr("Method")
                    path: "settings.injection.backend"
                    defaultValue: "auto"
                    options: [{ value: "auto", text: qsTr("Automatic") }, { value: "wayland-virtual-input", text: qsTr("Wayland virtual keyboard") },
                              { value: "xdg-portal-remotedesktop", text: qsTr("Desktop portal (GNOME, KDE)") }, { value: "x11-xtest", text: qsTr("X11 XTest") },
                              { value: "uinput", text: qsTr("uinput (advanced)") }]
                }
                SettingSwitch {
                    label: qsTr("Allow uinput")
                    description: qsTr("Last resort for desktops without another method. Needs the opt-in udev rule from the installation guide; any program in the input group could then create input devices.")
                    path: "settings.injection.allowUinput"
                    defaultValue: false
                }
                SettingRow {
                    label: qsTr("Keyboard layout for shortcuts")
                    description: qsTr("“auto” follows your session. Set e.g. “de” or “fr(azerty)” if shortcuts type the wrong keys.")
                    TextField { implicitWidth: 200; text: backend.get("settings.injection.keyboardLayout") || "auto"; onEditingFinished: backend.set("settings.injection.keyboardLayout", text) }
                }
            }

            Card {
                title: qsTr("Knowing which app is focused")
                subtitle: qsTr("Needed for app profiles. Window titles are never stored or logged.")
                Repeater {
                    model: view.ctx.trackers || []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.label + (view.ctx.active === modelData.id ? qsTr("  (in use)") : "") }
                            Label { text: modelData.detail || ""; opacity: 0.6; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        StatusPill { state: modelData.state }
                    }
                }
                Label {
                    visible: (view.ctx.trackers || []).length === 0
                    text: qsTr("No focus tracking in this session. Use profile locking instead.")
                    opacity: 0.7
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    font.pixelSize: 12
                    text: qsTr("GNOME needs the OpenDial Shell extension (gnome-extensions enable opendial@joshshearer.github.io). KDE Plasma loads the OpenDial KWin script automatically.")
                }
                SettingSwitch { label: qsTr("Switch profiles automatically"); path: "settings.context.enabled"; defaultValue: true }
                SettingCombo {
                    label: qsTr("Tool after switching apps")
                    path: "settings.context.toolOnSwitch"
                    defaultValue: "remember"
                    options: [{ value: "remember", text: qsTr("Last tool used in that app") }, { value: "default", text: qsTr("The profile's default tool") }]
                }
            }

            Card {
                title: qsTr("System controls")
                Repeater {
                    model: [
                        { n: qsTr("Volume"), c: view.caps.audio || {}, d: (view.caps.audio || {}).backend },
                        { n: qsTr("Media players"), c: view.caps.media || {}, d: ((view.caps.media || {}).players || []).map((p) => p.replace("org.mpris.MediaPlayer2.", "")).join(", ") || qsTr("none running: media keys are used") },
                        { n: qsTr("Brightness"), c: view.caps.brightness || {}, d: (view.caps.brightness || {}).detail },
                        { n: qsTr("Workspaces"), c: view.caps.workspace || {}, d: (view.caps.workspace || {}).method }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.n; Layout.preferredWidth: 140 }
                        Label { text: modelData.d || ""; opacity: 0.6; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        StatusPill { state: modelData.c.state || "not-supported" }
                    }
                }
                SettingSwitch {
                    label: qsTr("External monitor brightness (DDC/CI)")
                    description: qsTr("Uses ddcutil. Slow and not supported by every monitor.")
                    path: "settings.integrations.ddcutil"
                    defaultValue: false
                }
                SettingSwitch {
                    label: qsTr("Volume, media and brightness on the lock screen")
                    description: qsTr("Everything else is always disabled while the session is locked or a password prompt is focused.")
                    path: "settings.security.allowMediaWhenLocked"
                    defaultValue: true
                }
            }

            Card {
                title: qsTr("Approvals")
                subtitle: qsTr("Actions that run programs, open things or talk to other services only run after you allow them here.")
                Repeater {
                    model: view.approvalList
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            // plain text: both can come from an imported profile
                            Label { text: modelData.description; textFormat: Text.PlainText; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            Label { text: JSON.stringify(modelData.action).replace(/[^\x20-\x7e]/g, c => "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0")); textFormat: Text.PlainText; opacity: 0.6; font.pixelSize: 11; font.family: "monospace"; elide: Text.ElideRight; Layout.fillWidth: true }
                        }
                        StatusPill { state: modelData.approved ? "ok" : "warn"; text: modelData.approved ? qsTr("Allowed") : qsTr("Waiting") }
                        Button {
                            text: modelData.approved ? qsTr("Revoke") : qsTr("Allow")
                            highlighted: !modelData.approved
                            onClicked: { modelData.approved ? backend.revokeApproval(modelData.fingerprint) : backend.approve(modelData.fingerprint); view.reloadApprovals() }
                        }
                    }
                }
                Label { visible: view.approvalList.length === 0; text: qsTr("Nothing to approve."); opacity: 0.6 }
            }

            Card {
                title: qsTr("Plugins")
                subtitle: qsTr("Apps such as Krita can add their own Dial tools. They appear only after you allow them.")
                SettingSwitch { label: qsTr("Allow plugins to register"); path: "settings.integrations.plugins.enabled"; defaultValue: true }
                Repeater {
                    model: backend.state.plugins || []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: (modelData.name || modelData.appId); font.weight: Font.DemiBold }
                            Label {
                                text: modelData.registered === false ? qsTr("not running") : qsTr("%1 tool(s)").arg(modelData.tools) + ((modelData.matchAppIds || []).length ? qsTr(" in ") + modelData.matchAppIds.join(", ") : "")
                                opacity: 0.6; font.pixelSize: 12
                            }
                        }
                        Switch { checked: modelData.approved; onToggled: backend.approvePlugin(modelData.appId, checked); Accessible.name: qsTr("Allow %1").arg(modelData.appId) }
                    }
                }
                Label { visible: (backend.state.plugins || []).length === 0; text: qsTr("No plugins have registered."); opacity: 0.6 }
            }

            Card {
                title: qsTr("Local HTTP API")
                subtitle: qsTr("For scripts and stream decks. Listens on 127.0.0.1 only and needs the token in $XDG_RUNTIME_DIR/opendial/http-token.")
                SettingSwitch { label: qsTr("Enable"); path: "settings.integrations.httpApi.enabled"; defaultValue: false }
                SettingRow {
                    label: qsTr("Port")
                    SpinBox { from: 1024; to: 65535; editable: true; value: backend.get("settings.integrations.httpApi.port") || 47311; onValueModified: backend.set("settings.integrations.httpApi.port", value) }
                }
            }
        }
        Item { implicitHeight: 24 }
    }
}
