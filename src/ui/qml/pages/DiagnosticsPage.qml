// SPDX-License-Identifier: MIT
import QtCore
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"

Item {
    id: page
    property bool raw: false
    readonly property bool redact: { backend.config; return backend.get("settings.privacy.redactDiagnostics") !== false }
    Component.onDestruction: if (raw) backend.setRawStreaming(false)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12
        RowLayout {
            Label { text: qsTr("Diagnostics"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.fillWidth: true }
            CheckBox {
                text: qsTr("Raw device events")
                checked: page.raw
                enabled: backend.daemonRunning
                onToggled: { page.raw = checked; backend.setRawStreaming(checked) }
            }
            Button { text: qsTr("Clear"); onClicked: backend.clearEvents() }
            Button { text: qsTr("Copy report"); enabled: backend.daemonRunning; onClicked: { backend.copyToClipboard(JSON.stringify(backend.diagnostics(page.redact), null, 2)); copied.open() } }
            Button { text: qsTr("Export…"); highlighted: true; enabled: backend.daemonRunning; onClicked: saveDlg.open() }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: page.redact ? qsTr("Reports are redacted: Bluetooth addresses, window titles and paths are removed. Attach one when reporting a problem.") : qsTr("Redaction is off (Settings → Privacy): reports include Bluetooth addresses and paths.")
        }
        RowLayout {
            spacing: 24
            Label { text: qsTr("Last action: %1").arg(backend.state.lastAction || "—") }
            Label { text: qsTr("Latency: %1 ms").arg(((backend.state.lastActionLatencyUs || 0) / 1000).toFixed(2)) }
            Label { text: qsTr("Focus: %1").arg((backend.state.focus || {}).appId || "—") }
        }
        Pane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            background: Rectangle { color: Material.theme === Material.Dark ? "#101216" : "#f4f5f7"; radius: 8 }
            ListView {
                id: log
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                model: backend.events
                ScrollBar.vertical: ScrollBar {}
                delegate: Label {
                    required property var modelData
                    width: log.width
                    font.family: "monospace"
                    font.pixelSize: 12
                    color: modelData.source === "raw" ? Qt.rgba(0.5, 0.6, 0.7, 1) : Material.foreground
                    text: {
                        const e = modelData
                        if (e.source === "raw") return "raw   type=" + e.type + " code=" + e.code + " value=" + e.value + "  (" + e.node + ")"
                        let s = (e.kind || "").padEnd(14)
                        if (e.degrees !== undefined) s += (e.degrees > 0 ? "+" : "") + Number(e.degrees).toFixed(1) + "°" + (e.pressed ? " pressed" : "")
                        if (e.count !== undefined) s += "×" + e.count
                        return s
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: log.count === 0
                    text: qsTr("Press or turn the Dial. Events appear here.")
                    opacity: 0.5
                }
            }
        }
        RowLayout {
            Label { text: qsTr("Try without a Dial:"); opacity: 0.7 }
            Button { text: "↺"; enabled: backend.daemonRunning; onClicked: backend.simulate({ rotate: -10 }) }
            Button { text: "↻"; enabled: backend.daemonRunning; onClicked: backend.simulate({ rotate: 10 }) }
            Button { text: qsTr("Click"); enabled: backend.daemonRunning; onClicked: backend.simulate({ click: 1 }) }
            Button { text: qsTr("Hold"); enabled: backend.daemonRunning; onClicked: backend.simulate({ longPress: true }) }
        }
    }
    FileDialog {
        id: saveDlg
        fileMode: FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        nameFilters: [qsTr("JSON (*.json)")]
        onAccepted: { const e = backend.exportDiagnostics(selectedFile, page.redact); if (e.length) { copied.text = e; copied.open() } }
    }
    ToolTip { id: copied; text: qsTr("Copied"); timeout: 2000; x: (page.width - width) / 2; y: page.height - 80 }
}
