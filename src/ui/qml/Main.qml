// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: win
    width: 1180
    height: 800
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: qsTr("OpenDial")

    readonly property string themeSetting: backend.get("settings.menu.theme") || "system"
    Material.theme: themeSetting === "dark" ? Material.Dark : themeSetting === "light" ? Material.Light : Material.System
    Material.accent: backend.get("settings.menu.accent") || "#4c8dff"
    Material.primary: Material.accent

    property int page: 0
    property string initialPage: ""
    readonly property var pages: [
        { id: "dashboard", name: qsTr("Dashboard"), icon: "home", src: "pages/DashboardPage.qml" },
        { id: "menu", name: qsTr("Radial menu"), icon: "dial", src: "pages/MenuPage.qml" },
        { id: "profiles", name: qsTr("App profiles"), icon: "profiles", src: "pages/ProfilesPage.qml" },
        { id: "rotation", name: qsTr("Rotation & haptics"), icon: "rotate", src: "pages/RotationPage.qml" },
        { id: "integrations", name: qsTr("Integrations"), icon: "integrations", src: "pages/IntegrationsPage.qml" },
        { id: "device", name: qsTr("Bluetooth & device"), icon: "bluetooth", src: "pages/DevicePage.qml" },
        { id: "diagnostics", name: qsTr("Diagnostics"), icon: "diagnostics", src: "pages/DiagnosticsPage.qml" },
        { id: "settings", name: qsTr("Settings"), icon: "settings", src: "pages/SettingsPage.qml" }
    ]
    function go(id) { for (let i = 0; i < pages.length; ++i) if (pages[i].id === id) page = i }
    property string editProfile: "global"   // shared between Profiles and Menu pages

    Component.onCompleted: {
        if (initialPage.length) go(initialPage)
        if (backend.daemonRunning && backend.state.firstRun === true && initialPage.length === 0)
            wizard.open()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------------------------------------------------------- navigation
        Pane {
            Layout.fillHeight: true
            Layout.preferredWidth: win.width < 960 ? 72 : 236
            padding: 0
            Material.elevation: 2
            ColumnLayout {
                anchors.fill: parent
                spacing: 2
                RowLayout {
                    Layout.margins: 16
                    spacing: 10
                    Image { source: "image://opendial/dial?color=" + Material.accent.toString().substring(1); sourceSize: Qt.size(56, 56); Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                    Label { visible: win.width >= 960; text: "OpenDial"; font.pixelSize: 20; font.weight: Font.DemiBold }
                }
                Repeater {
                    model: win.pages
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        highlighted: win.page === index
                        onClicked: win.page = index
                        Accessible.name: modelData.name
                        contentItem: RowLayout {
                            spacing: 14
                            Icon { name: modelData.icon; size: 22; color: highlighted ? Material.accent : Material.foreground; Layout.leftMargin: 6 }
                            Label { visible: win.width >= 960; text: modelData.name; Layout.fillWidth: true; color: highlighted ? Material.accent : Material.foreground }
                        }
                        ToolTip.visible: win.width < 960 && hovered
                        ToolTip.text: modelData.name
                    }
                }
                Item { Layout.fillHeight: true }
                Label {
                    visible: win.width >= 960
                    Layout.margins: 16
                    text: "v" + backend.version
                    opacity: 0.5
                    font.pixelSize: 12
                }
            }
        }

        // ---------------------------------------------------------------- content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // banners
            Pane {
                visible: !backend.daemonRunning
                Layout.fillWidth: true
                Material.background: "#7a3b10"
                Material.foreground: "white"
                RowLayout {
                    anchors.fill: parent
                    Label { text: qsTr("The OpenDial service is not running. Changes are saved to the file and used when it starts."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Button { text: qsTr("Start service"); onClicked: backend.startDaemon() }
                }
            }
            Pane {
                visible: backend.errors.length > 0
                Layout.fillWidth: true
                Material.background: "#6b1f1f"
                Material.foreground: "white"
                RowLayout {
                    anchors.fill: parent
                    Label {
                        text: qsTr("Not applied: ") + backend.errors.slice(0, 3).join("; ") + (backend.errors.length > 3 ? " …" : "")
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    Button { text: qsTr("Undo changes"); onClicked: backend.revert() }
                }
            }
            Pane {
                visible: backend.state.suppressed === true
                Layout.fillWidth: true
                Material.background: "#4a4a14"
                Material.foreground: "white"
                RowLayout {
                    anchors.fill: parent
                    Label { text: qsTr("Dial input is paused: %1").arg(backend.state.suppressReason || ""); Layout.fillWidth: true }
                    Button { visible: backend.state.paused === true; text: qsTr("Resume"); onClicked: backend.setPaused(false) }
                }
            }

            Loader {
                id: pageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: win.pages[win.page].src
            }
        }
    }

    // transient messages
    ToolTip {
        id: toast
        timeout: 4000
        x: (win.width - width) / 2
        y: win.height - height - 32
    }
    Connections {
        target: backend
        function onMessageChanged() { toast.text = backend.lastMessage; toast.open() }
    }

    Wizard { id: wizard }
}
