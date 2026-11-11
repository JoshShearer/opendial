// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

Item {
    id: page
    readonly property var profiles: { backend.config; return backend.get("profiles") || [] }
    property int pIndex: Math.max(0, backend.profileIndex(win.editProfile))
    readonly property var profile: profiles[pIndex] || ({})
    readonly property string base: "profiles." + pIndex + ".tools"
    readonly property bool isGlobal: profile.match && profile.match.kind === "global"
    property string selectedPath: ""

    // flatten tools of the edited profile into rows with config paths
    function flatten(tools, path, depth, out) {
        for (let i = 0; i < tools.length; ++i) {
            const t = tools[i]
            out.push({ tool: t, path: path + "." + i, parent: path, index: i, count: tools.length, depth: depth })
            if (t.kind === "folder")
                flatten(t.children || [], path + "." + i + ".children", depth + 1, out)
        }
        return out
    }
    readonly property var rows: flatten(profile.tools || [], base, 0, [])
    readonly property var folders: rows.filter((r) => r.tool.kind === "folder")

    // menu preview: own tools + global ones in extend mode
    readonly property var previewTools: {
        let t = (profile.tools || []).filter((x) => x.enabled !== false)
        if (!isGlobal && profile.menuMode !== "replace") {
            const g = profiles.find((p) => p.match && p.match.kind === "global")
            if (g) t = t.concat((g.tools || []).filter((x) => x.enabled !== false && !t.some((o) => o.id === x.id)))
        }
        return t
    }
    property int previewHighlight: 0
    readonly property int maxPerRing: backend.get("settings.menu.maxPerRing") || 7
    readonly property var previewItems: {
        const items = []
        const all = previewTools
        const paged = all.length > maxPerRing
        const n = paged ? maxPerRing - 1 : all.length
        for (let i = 0; i < n && i < all.length; ++i)
            items.push({ id: all[i].id, name: all[i].name, icon: all[i].kind === "folder" ? "folder" : all[i].icon, hint: backend.describeAction((all[i].bindings || {}).rotate_cw || { type: "none" }),
                         folder: all[i].kind === "folder", active: all[i].id === backend.state.activeTool })
        if (paged) items.push({ id: "__more", name: qsTr("More"), icon: "more", hint: "", synthetic: true })
        return items
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        // ------------------------------------------------------------ left: list
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            RowLayout {
                Label { text: qsTr("Radial menu"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.fillWidth: true }
                ComboBox {
                    Layout.preferredWidth: 260
                    model: page.profiles.map((p) => p.name)
                    currentIndex: page.pIndex
                    onActivated: win.editProfile = page.profiles[currentIndex].id
                    Accessible.name: qsTr("Profile to edit")
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.75
                text: page.isGlobal ? qsTr("These tools appear in every app. Press and hold the Dial to open the menu, turn to highlight, click to select.")
                                    : page.profile.menuMode === "replace"
                                      ? qsTr("Only these tools appear while this app is focused.")
                                      : qsTr("These tools appear first while this app is focused, followed by the All apps tools.")
            }
            RowLayout {
                Button {
                    text: qsTr("Add tool")
                    icon.source: "image://opendial/add?color=ffffff"
                    highlighted: true
                    onClicked: addMenu.open()
                    Menu {
                        id: addMenu
                        MenuItem { text: qsTr("Custom tool (keyboard shortcuts)…"); onTriggered: { const t = backend.newTool(qsTr("Custom tool")); backend.insert(page.base, -1, t); page.selectedPath = page.base + "." + (page.profile.tools || []).length; editor.open() } }
                        MenuItem { text: qsTr("Built-in tool…"); onTriggered: catalog.open() }
                        MenuItem { text: qsTr("Folder"); onTriggered: backend.insert(page.base, -1, { id: backend.uniqueId("folder"), name: qsTr("Folder"), icon: "folder", kind: "folder", enabled: true, children: [] }) }
                    }
                }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("%1 tools").arg(page.rows.filter((r) => r.tool.kind !== "folder").length); opacity: 0.6 }
            }

            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: page.rows
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: list.width
                    highlighted: page.selectedPath === modelData.path
                    onClicked: { page.selectedPath = modelData.path; if (modelData.tool.kind !== "folder") editor.open() }
                    Accessible.name: modelData.tool.name
                    contentItem: RowLayout {
                        spacing: 12
                        Item { Layout.preferredWidth: row.modelData.depth * 28 }
                        Icon { name: row.modelData.tool.kind === "folder" ? "folder" : row.modelData.tool.icon; size: 26; opacity: row.modelData.tool.enabled === false ? 0.4 : 1 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label { text: row.modelData.tool.name; font.weight: Font.DemiBold; opacity: row.modelData.tool.enabled === false ? 0.5 : 1 }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                opacity: 0.6
                                font.pixelSize: 12
                                text: {
                                    const t = row.modelData.tool
                                    if (t.kind === "folder") return qsTr("%1 tools inside").arg((t.children || []).length)
                                    const b = t.bindings || {}
                                    const parts = []
                                    if (b.rotate_cw) parts.push("↻ " + backend.describeAction(b.rotate_cw))
                                    if (b.rotate_ccw) parts.push("↺ " + backend.describeAction(b.rotate_ccw))
                                    if (b.click) parts.push("● " + backend.describeAction(b.click))
                                    return parts.join("   ")
                                }
                            }
                        }
                        Switch {
                            checked: row.modelData.tool.enabled !== false
                            onToggled: backend.set(row.modelData.path + ".enabled", checked)
                            Accessible.name: qsTr("Show %1 in the menu").arg(row.modelData.tool.name)
                        }
                        ToolButton {
                            icon.source: "image://opendial/up?color=" + Material.foreground.toString().substring(1)
                            enabled: row.modelData.index > 0
                            onClicked: backend.move(row.modelData.parent, row.modelData.index, row.modelData.index - 1)
                            Accessible.name: qsTr("Move up")
                        }
                        ToolButton {
                            icon.source: "image://opendial/down?color=" + Material.foreground.toString().substring(1)
                            enabled: row.modelData.index < row.modelData.count - 1
                            onClicked: backend.move(row.modelData.parent, row.modelData.index, row.modelData.index + 1)
                            Accessible.name: qsTr("Move down")
                        }
                        ToolButton {
                            text: "⋯"
                            onClicked: rowMenu.popup()
                            Accessible.name: qsTr("More actions")
                            Menu {
                                id: rowMenu
                                MenuItem { text: qsTr("Edit…"); enabled: row.modelData.tool.kind !== "folder"; onTriggered: { page.selectedPath = row.modelData.path; editor.open() } }
                                MenuItem { text: qsTr("Rename…"); onTriggered: { renameDlg.path = row.modelData.path; renameDlg.text = row.modelData.tool.name; renameDlg.open() } }
                                MenuItem { text: qsTr("Make active tool"); enabled: row.modelData.tool.kind !== "folder" && backend.daemonRunning; onTriggered: backend.setActiveTool(row.modelData.tool.id) }
                                MenuItem {
                                    text: qsTr("Duplicate")
                                    onTriggered: {
                                        const c = JSON.parse(JSON.stringify(row.modelData.tool))
                                        c.id = backend.uniqueId(c.id); c.name = c.name + qsTr(" copy"); c.builtin = false
                                        backend.insert(row.modelData.parent, row.modelData.index + 1, c)
                                    }
                                }
                                Menu {
                                    title: qsTr("Move to folder")
                                    enabled: row.modelData.tool.kind !== "folder"
                                    Repeater {
                                        model: page.folders.filter((f) => f.path !== row.modelData.parent.replace(/\.children$/, ""))
                                        MenuItem {
                                            required property var modelData
                                            text: modelData.tool.name
                                            onTriggered: {
                                                const t = row.modelData.tool
                                                const dest = modelData.path + ".children"
                                                backend.remove(row.modelData.path)
                                                // removing may shift indices of a later folder in the same array
                                                let d = dest
                                                if (row.modelData.parent === modelData.parent && modelData.index > row.modelData.index)
                                                    d = modelData.parent + "." + (modelData.index - 1) + ".children"
                                                backend.insert(d, -1, t)
                                            }
                                        }
                                    }
                                    MenuItem {
                                        text: qsTr("Top level")
                                        enabled: row.modelData.depth > 0
                                        onTriggered: { const t = row.modelData.tool; backend.remove(row.modelData.path); backend.insert(page.base, -1, t) }
                                    }
                                }
                                MenuSeparator {}
                                MenuItem { text: qsTr("Remove"); onTriggered: backend.remove(row.modelData.path) }
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------ right: preview + settings
        ScrollView {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: 392
                spacing: 16
                Card {
                    title: qsTr("Preview")
                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 340
                        implicitHeight: 340
                        RadialMenu {
                            id: preview
                            anchors.centerIn: parent
                            menuSize: 340
                            dark: win.pal.dark
                            background: win.pal.background
                            foreground: win.pal.foreground
                            foregroundDim: win.pal.foregroundDim
                            accent: win.pal.accent
                            accentForeground: win.pal.accentForeground
                            menu: ({ items: page.previewItems, highlight: Math.min(page.previewHighlight, page.previewItems.length - 1), breadcrumb: page.profile.name, pageCount: 1, page: 0 })
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: (m) => {
                                const a = Math.atan2(m.y - height / 2, m.x - width / 2) + Math.PI / 2
                                const n = page.previewItems.length
                                if (n === 0) return
                                page.previewHighlight = ((Math.round(a / (2 * Math.PI) * n) % n) + n) % n
                            }
                            onWheel: (w) => {
                                const n = page.previewItems.length
                                if (n) page.previewHighlight = ((page.previewHighlight + (w.angleDelta.y < 0 ? 1 : -1)) % n + n) % n
                            }
                        }
                    }
                    Label { text: qsTr("Scroll or click to try the highlight."); opacity: 0.6; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
                    Button { text: qsTr("Show the real menu"); enabled: backend.daemonRunning; onClicked: backend.openMenu(); Layout.alignment: Qt.AlignHCenter }
                }
                Card {
                    visible: !page.isGlobal
                    title: qsTr("This profile")
                    SettingCombo {
                        label: qsTr("Menu contents")
                        path: "profiles." + page.pIndex + ".menuMode"
                        defaultValue: "extend"
                        options: [{ value: "extend", text: qsTr("App tools + all-app tools") }, { value: "replace", text: qsTr("Only app tools") }]
                    }
                }
                Card {
                    title: qsTr("Menu behaviour")
                    SettingCombo {
                        label: qsTr("Open the menu with")
                        path: "settings.menu.openGesture"
                        defaultValue: "long_press"
                        options: [{ value: "long_press", text: qsTr("Press and hold") }, { value: "double_click", text: qsTr("Double-click") },
                                  { value: "triple_click", text: qsTr("Triple-click") }, { value: "none", text: qsTr("Never (shortcuts only)") }]
                    }
                    SettingCombo {
                        label: qsTr("Where it appears")
                        path: "settings.menu.placement"
                        defaultValue: "pointer"
                        options: [{ value: "pointer", text: qsTr("At the mouse pointer") }, { value: "center", text: qsTr("Centre of the screen") }, { value: "saved", text: qsTr("Saved position") }]
                    }
                    SettingSlider { label: qsTr("Size"); path: "settings.menu.size"; from: 240; to: 600; stepSize: 10; defaultValue: 360; unit: " px" }
                    SettingSlider { label: qsTr("Turn per item"); path: "settings.menu.detentDegrees"; from: 10; to: 45; stepSize: 1; defaultValue: 20; unit: "°" }
                    SettingSlider { label: qsTr("Items per ring"); path: "settings.menu.maxPerRing"; from: 4; to: 12; defaultValue: 7 }
                    SettingSlider { label: qsTr("Close after"); description: qsTr("0 = stay open"); path: "settings.menu.timeoutMs"; from: 0; to: 20000; stepSize: 500; defaultValue: 8000; unit: " ms" }
                    SettingSwitch { label: qsTr("Wrap around"); path: "settings.menu.wrap"; defaultValue: true }
                    SettingSwitch { label: qsTr("Show hints"); description: qsTr("What turning and clicking do"); path: "settings.menu.showHints"; defaultValue: true }
                    SettingSwitch { label: qsTr("Close when the app changes"); path: "settings.menu.closeOnFocusChange"; defaultValue: true }
                    SettingSwitch { label: qsTr("Reduce motion"); path: "settings.menu.reduceMotion"; defaultValue: false }
                }
                Card {
                    title: qsTr("Appearance")
                    SettingCombo {
                        label: qsTr("Theme")
                        path: "settings.menu.theme"
                        defaultValue: "system"
                        options: [{ value: "system", text: qsTr("Follow system") }, { value: "dark", text: qsTr("Dark") }, { value: "light", text: qsTr("Light") }]
                    }
                    SettingRow {
                        label: qsTr("Accent colour")
                        Row {
                            spacing: 6
                            readonly property string current: backend.get("settings.menu.accent") || "system"
                            Repeater {
                                // "system" follows the desktop accent (Omarchy theme, GNOME/KDE accent)
                                model: ["system", "#4c8dff", "#7c5cff", "#e0527a", "#f08c2e", "#2fb37b", "#16a2b8", "#8a8f98"]
                                delegate: Rectangle {
                                    required property string modelData
                                    readonly property bool isSystem: modelData === "system"
                                    width: 26; height: 26; radius: 13
                                    color: isSystem ? win.pal.accent : modelData
                                    border.width: parent.current === modelData ? 3 : 0
                                    border.color: Material.foreground
                                    Label {
                                        visible: parent.isSystem
                                        anchors.centerIn: parent
                                        text: "A"
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: win.pal.accentForeground
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: backend.set("settings.menu.accent", parent.modelData) }
                                    ToolTip.visible: isSystem && hover.hovered
                                    ToolTip.text: qsTr("Follow the system accent")
                                    HoverHandler { id: hover }
                                    Accessible.name: isSystem ? qsTr("System accent") : modelData
                                    Accessible.role: Accessible.Button
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.6
                        font.pixelSize: 12
                        text: win.pal.source === "omarchy" ? qsTr("Following the Omarchy theme.")
                            : win.pal.source === "portal" ? qsTr("Following the desktop's light/dark setting.")
                            : qsTr("Using the built-in colours.")
                    }
                    SettingSlider { label: qsTr("Opacity"); path: "settings.menu.opacity"; from: 0.5; to: 1; stepSize: 0.02; decimals: 2; defaultValue: 0.94 }
                }
            }
        }
    }

    ToolEditor { id: editor; path: page.selectedPath }

    Dialog {
        id: renameDlg
        property string path: ""
        property alias text: renameField.text
        title: qsTr("Rename")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: renameField; width: 300; onAccepted: renameDlg.accept() }
        onAccepted: backend.set(path + ".name", renameField.text)
    }

    Dialog {
        id: catalog
        title: qsTr("Add a built-in tool")
        anchors.centerIn: parent
        width: 560
        height: Math.min(620, page.height - 40)
        modal: true
        standardButtons: Dialog.Close
        ListView {
            anchors.fill: parent
            clip: true
            model: backend.builtinTools()
            section.property: "catalogGroup"
            section.delegate: Label { text: section || qsTr("General"); font.weight: Font.DemiBold; topPadding: 10; opacity: 0.7 }
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width
                contentItem: RowLayout {
                    spacing: 12
                    Icon { name: modelData.icon; size: 24 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label { text: modelData.name; font.weight: Font.DemiBold }
                        Label { text: modelData.description || ""; opacity: 0.6; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
                onClicked: {
                    const t = JSON.parse(JSON.stringify(modelData))
                    delete t.catalogGroup
                    t.id = backend.uniqueId(t.id)
                    backend.insert(page.base, -1, t)
                    catalog.close()
                }
            }
        }
    }
}
