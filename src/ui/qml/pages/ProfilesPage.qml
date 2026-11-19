// SPDX-License-Identifier: MIT
import QtCore
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"

Item {
    id: page
    readonly property var profiles: { backend.config; return backend.get("profiles") || [] }
    readonly property int sel: Math.max(0, backend.profileIndex(win.editProfile))
    readonly property var p: profiles[sel] || ({})
    readonly property string pp: "profiles." + sel
    readonly property var match: p.match || ({})
    readonly property bool isGlobal: match.kind === "global"
    readonly property var apps: backend.installedApps()
    property int countdown: 0

    Timer {
        id: pickTimer
        interval: 1000
        repeat: true
        onTriggered: {
            page.countdown--
            if (page.countdown <= 0) {
                stop()
                const f = backend.state.focus || {}
                if (f.appId && f.appId !== "io.github.joshshearer.opendial.opendial" && f.appId !== "opendial") {
                    const ids = (page.match.appIds || []).slice()
                    if (ids.indexOf(f.appId) < 0) ids.push(f.appId)
                    backend.set(page.pp + ".match.appIds", ids)
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            Label { text: qsTr("App profiles"); font.pixelSize: 26; font.weight: Font.DemiBold }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.75
                text: qsTr("The Dial switches tools automatically when you focus an app with its own profile.")
            }
            RowLayout {
                Button { text: qsTr("New profile"); highlighted: true; onClicked: newDlg.open() }
                Button { text: qsTr("Import…"); onClicked: importDlg.open() }
            }
            ListView {
                id: plist
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: page.profiles
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: plist.width
                    highlighted: index === page.sel
                    onClicked: win.editProfile = modelData.id
                    contentItem: RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: modelData.name; font.weight: Font.DemiBold; opacity: modelData.enabled === false ? 0.5 : 1 }
                            Label {
                                text: modelData.match.kind === "global" ? qsTr("All apps (fallback)")
                                      : (modelData.match.appIds || modelData.match.workspaces || []).slice(0, 3).join(", ")
                                opacity: 0.6; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
                            }
                        }
                        Label { visible: (backend.state.profile || {}).profileId === modelData.id; text: qsTr("active"); color: Material.accent; font.pixelSize: 12 }
                        Label { visible: backend.state.lockedProfile === modelData.id; text: "🔒" }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 16
                Card {
                    title: page.p.name || ""
                    subtitle: page.isGlobal ? qsTr("Used whenever no other profile matches.") : ""
                    SettingRow {
                        label: qsTr("Name")
                        TextField { implicitWidth: 260; text: page.p.name || ""; onEditingFinished: backend.set(page.pp + ".name", text) }
                    }
                    SettingSwitch { visible: !page.isGlobal; label: qsTr("Enabled"); path: page.pp + ".enabled"; defaultValue: true }
                    SettingRow {
                        label: qsTr("Default tool")
                        description: qsTr("Selected the first time this profile becomes active")
                        ComboBox {
                            implicitWidth: 260
                            readonly property var tools: (page.p.tools || []).filter((t) => t.kind !== "folder")
                            model: [qsTr("(first tool)")].concat(tools.map((t) => t.name))
                            currentIndex: { const i = tools.findIndex((t) => t.id === page.p.defaultTool); return i + 1 }
                            onActivated: currentIndex === 0 ? backend.remove(page.pp + ".defaultTool") : backend.set(page.pp + ".defaultTool", tools[currentIndex - 1].id)
                        }
                    }
                    SettingRow {
                        visible: !page.isGlobal
                        label: qsTr("Priority")
                        description: qsTr("When two profiles match, the higher priority wins")
                        SpinBox { from: -100; to: 100; value: page.p.priority || 0; onValueModified: backend.set(page.pp + ".priority", value) }
                    }
                    RowLayout {
                        Button { text: qsTr("Edit tools"); highlighted: true; onClicked: win.go("menu") }
                        Button {
                            text: backend.state.lockedProfile === page.p.id ? qsTr("Unlock") : qsTr("Lock")
                            enabled: backend.daemonRunning
                            onClicked: backend.state.lockedProfile === page.p.id ? backend.unlockProfile() : backend.lockProfile(page.p.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Locking keeps this profile active in every app")
                        }
                        Button { text: qsTr("Export…"); onClicked: exportDlg.open() }
                        Button {
                            text: qsTr("Duplicate")
                            onClicked: {
                                const c = JSON.parse(JSON.stringify(page.p))
                                c.id = backend.uniqueId(c.id); c.name = c.name + qsTr(" copy")
                                if (c.match.kind === "global") c.match = { kind: "app", appIds: [] }
                                backend.insert("profiles", -1, c)
                            }
                        }
                        Button {
                            visible: !page.isGlobal
                            text: qsTr("Delete")
                            onClicked: { const id = page.p.id; win.editProfile = "global"; backend.remove("profiles." + backend.profileIndex(id)) }
                        }
                    }
                }

                Card {
                    visible: !page.isGlobal
                    title: qsTr("When is it used?")
                    SettingCombo {
                        label: qsTr("Match")
                        path: page.pp + ".match.kind"
                        defaultValue: "app"
                        options: [{ value: "app", text: qsTr("When an app is focused") }, { value: "window", text: qsTr("When a specific window is focused") },
                                  { value: "workspace", text: qsTr("On a workspace") }, { value: "manual", text: qsTr("Only when locked manually") }]
                    }
                    ColumnLayout {
                        visible: page.match.kind === "app" || page.match.kind === "window"
                        Layout.fillWidth: true
                        Label { text: qsTr("Apps"); font.weight: Font.DemiBold }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: page.match.appIds || []
                                delegate: Button {
                                    required property string modelData
                                    required property int index
                                    text: modelData + "  ✕"
                                    flat: true
                                    onClicked: backend.remove(page.pp + ".match.appIds." + index)
                                    Accessible.name: qsTr("Remove %1").arg(modelData)
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: appBox
                                Layout.fillWidth: true
                                editable: true
                                model: page.apps.map((a) => a.name + "  (" + a.id + ")")
                                Accessible.name: qsTr("Add app")
                            }
                            Button {
                                text: qsTr("Add")
                                onClicked: {
                                    let id = appBox.editText
                                    const i = appBox.find(appBox.editText)
                                    if (i >= 0) { const a = page.apps[i]; id = a.wmClass || a.id }
                                    id = id.trim()
                                    if (!id.length) return
                                    const ids = (page.match.appIds || []).slice()
                                    if (ids.indexOf(id) < 0) ids.push(id)
                                    backend.set(page.pp + ".match.appIds", ids)
                                    appBox.editText = ""
                                }
                            }
                            Button {
                                text: page.countdown > 0 ? qsTr("Switch to the app… %1").arg(page.countdown) : qsTr("Pick focused app")
                                enabled: backend.daemonRunning && page.countdown === 0
                                onClicked: { page.countdown = 4; pickTimer.start() }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Click, then focus the app within 4 seconds")
                            }
                        }
                    }
                    SettingRow {
                        visible: page.match.kind === "window"
                        label: qsTr("Window title contains (regular expression)")
                        TextField { implicitWidth: 260; text: page.match.titleRegex || ""; onEditingFinished: backend.set(page.pp + ".match.titleRegex", text) }
                    }
                    SettingRow {
                        visible: page.match.kind === "workspace"
                        label: qsTr("Workspaces (comma separated)")
                        TextField {
                            implicitWidth: 260
                            text: (page.match.workspaces || []).join(", ")
                            onEditingFinished: backend.set(page.pp + ".match.workspaces", text.split(",").map((s) => s.trim()).filter((s) => s.length))
                        }
                    }
                    Label {
                        visible: page.p.verified === false
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.6
                        font.pixelSize: 12
                        text: qsTr("This is a starter profile. Its shortcuts follow the app's documented defaults and were not tested on your setup.")
                    }
                }
            }
        }
    }

    Dialog {
        id: newDlg
        title: qsTr("New profile")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            TextField { id: newName; placeholderText: qsTr("Name, e.g. Darktable"); Layout.preferredWidth: 320 }
            TextField { id: newApp; placeholderText: qsTr("App id (optional), e.g. darktable"); Layout.preferredWidth: 320 }
        }
        onAccepted: {
            if (!newName.text.trim().length) return
            const p = backend.newProfile(newName.text.trim(), newApp.text.trim())
            backend.insert("profiles", -1, p)
            win.editProfile = p.id
            newName.text = ""; newApp.text = ""
        }
    }
    FileDialog {
        id: exportDlg
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("OpenDial profile (*.json)")]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: { const e = backend.exportProfile(page.p.id, selectedFile); if (e.length) errDlg.show(e) }
    }
    FileDialog {
        id: importDlg
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("OpenDial profile (*.json)")]
        onAccepted: { const e = backend.importProfile(selectedFile); if (e.length) errDlg.show(e) }
    }
    Dialog {
        id: errDlg
        function show(t) { errText.text = t; open() }
        title: qsTr("Something went wrong")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok
        Label { id: errText; wrapMode: Text.WordWrap; width: 380 }
    }
}
