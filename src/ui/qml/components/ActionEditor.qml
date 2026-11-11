// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Edits the action object stored at `path` in the configuration.
ColumnLayout {
    id: ed
    property string path: ""
    property int depth: 0
    property bool allowNone: true
    readonly property var action: { backend.config; const a = backend.get(path); return a ? a : ({ type: "none" }) }
    readonly property string type: action.type || "none"
    property bool advanced: false
    spacing: 8

    function setField(k, v) { backend.set(path + "." + k, v) }
    function setType(t) {
        const defaults = {
            none: {}, key: { keys: "" }, text: { text: "" }, mouse: { button: "left" },
            scroll: { axis: "vertical", amount: 1 }, zoom: { direction: "in", method: "keys" },
            volume: { delta: 2 }, mute: {}, media: { command: "playpause" }, brightness: { delta: 5 },
            workspace: { direction: "next" }, tool: { op: "next" }, profile: { op: "cycle" }, menu: { op: "toggle" },
            haptic: { waveform: "click" }, value: { variable: "value", step: 1, min: 0, max: 100, default: 50 },
            osc: { host: "127.0.0.1", port: 9000, address: "/dial", args: ["{value}"] },
            launch: { url: "https://" }, command: { argv: ["notify-send", "OpenDial"] },
            dbus: { service: "", path: "/", interface: "", method: "" },
            macro: { steps: [] }, "if": { condition: { appId: [] }, then: { type: "none" } },
            plugin: { appId: "", toolId: "" }, osd: { text: "" }
        }
        const o = Object.assign({ type: t }, defaults[t] || {})
        backend.set(path, o)
    }
    readonly property var typeNames: ({
        none: qsTr("Nothing"), key: qsTr("Keyboard shortcut"), text: qsTr("Type text"), mouse: qsTr("Mouse button"),
        scroll: qsTr("Scroll"), zoom: qsTr("Zoom"), volume: qsTr("Volume"), mute: qsTr("Mute / unmute"),
        media: qsTr("Media control"), brightness: qsTr("Screen brightness"), workspace: qsTr("Switch workspace"),
        tool: qsTr("Switch Dial tool"), profile: qsTr("Profile"), menu: qsTr("Radial menu"), haptic: qsTr("Haptic pulse"),
        value: qsTr("Adjust a value"), osc: qsTr("OSC message"), launch: qsTr("Open app, URL or file"),
        command: qsTr("Run a program"), dbus: qsTr("D-Bus method call"), macro: qsTr("Macro (sequence)"),
        "if": qsTr("Condition"), plugin: qsTr("Plugin event"), osd: qsTr("Show a message")
    })
    readonly property var typeModel: {
        const list = []
        const types = backend.actionTypes()
        for (let i = 0; i < types.length; ++i)
            if (ed.allowNone || types[i] !== "none")
                list.push({ value: types[i], text: ed.typeNames[types[i]] || types[i] })
        return list
    }

    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            id: typeBox
            Layout.preferredWidth: 230
            model: ed.typeModel
            textRole: "text"
            valueRole: "value"
            currentIndex: { for (let i = 0; i < ed.typeModel.length; ++i) if (ed.typeModel[i].value === ed.type) return i; return 0 }
            onActivated: ed.setType(currentValue)
        }
        StatusPill {
            visible: backend.riskOf(ed.action) === "approval"
            state: "warn"
            text: qsTr("Needs approval")
        }
        Item { Layout.fillWidth: true }
        ToolButton {
            text: ed.advanced ? qsTr("Form") : qsTr("JSON")
            onClicked: ed.advanced = !ed.advanced
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Edit this action as JSON")
        }
    }

    // ---------------------------------------------------------- JSON mode
    TextArea {
        visible: ed.advanced
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        font.family: "monospace"
        wrapMode: TextArea.Wrap
        text: JSON.stringify(ed.action, null, 2)
        onEditingFinished: {
            try { backend.set(ed.path, JSON.parse(text)) } catch (e) { }
        }
    }

    // ---------------------------------------------------------- forms
    Loader {
        Layout.fillWidth: true
        active: !ed.advanced
        sourceComponent: {
            switch (ed.type) {
            case "key": return keyForm
            case "text": return textForm
            case "mouse": return mouseForm
            case "scroll": return scrollForm
            case "zoom": return zoomForm
            case "volume": return volumeForm
            case "mute": return muteForm
            case "media": return mediaForm
            case "brightness": return deltaForm
            case "workspace": return workspaceForm
            case "tool": return toolForm
            case "profile": return profileForm
            case "menu": return menuForm
            case "haptic": return hapticForm
            case "value": return valueForm
            case "osc": return oscForm
            case "launch": return launchForm
            case "command": return commandForm
            case "macro": return macroForm
            case "osd": return osdForm
            case "plugin": return pluginForm
            default: return null
            }
        }
    }

    Component {
        id: keyForm
        ColumnLayout {
            KeyRecorder { Layout.fillWidth: true; keys: ed.action.keys || ""; onEdited: (k) => ed.setField("keys", k) }
            RowLayout {
                CheckBox {
                    text: qsTr("Hold modifiers between steps (Alt+Tab style)")
                    checked: ed.action.holdModifiers === true
                    onToggled: ed.setField("holdModifiers", checked)
                }
                SpinBox {
                    visible: ed.action.holdModifiers === true
                    from: 100; to: 5000; stepSize: 100; editable: true
                    value: ed.action.releaseAfterMs || 800
                    onValueModified: ed.setField("releaseAfterMs", value)
                }
                Label { visible: ed.action.holdModifiers === true; text: qsTr("ms"); opacity: 0.7 }
            }
        }
    }
    Component {
        id: textForm
        TextField { placeholderText: qsTr("Text to type"); text: ed.action.text || ""; onEditingFinished: ed.setField("text", text) }
    }
    Component {
        id: mouseForm
        ComboBox {
            model: [{ v: "left", t: qsTr("Left") }, { v: "right", t: qsTr("Right") }, { v: "middle", t: qsTr("Middle") }, { v: "back", t: qsTr("Back") }, { v: "forward", t: qsTr("Forward") }]
            textRole: "t"; valueRole: "v"
            currentIndex: Math.max(0, ["left", "right", "middle", "back", "forward"].indexOf(ed.action.button || "left"))
            onActivated: ed.setField("button", currentValue)
        }
    }
    Component {
        id: scrollForm
        RowLayout {
            ComboBox {
                model: [{ v: "vertical", t: qsTr("Vertical") }, { v: "horizontal", t: qsTr("Horizontal") }]
                textRole: "t"; valueRole: "v"
                currentIndex: ed.action.axis === "horizontal" ? 1 : 0
                onActivated: ed.setField("axis", currentValue)
            }
            Label { text: qsTr("Lines per step") }
            SpinBox {
                from: -20; to: 20; editable: true
                value: ed.action.amount !== undefined ? ed.action.amount : 1
                onValueModified: ed.setField("amount", value)
            }
            CheckBox { text: qsTr("Smooth"); checked: ed.action.smooth === true; onToggled: ed.setField("smooth", checked) }
        }
    }
    Component {
        id: zoomForm
        RowLayout {
            ComboBox {
                model: [{ v: "in", t: qsTr("Zoom in") }, { v: "out", t: qsTr("Zoom out") }, { v: "reset", t: qsTr("Reset zoom") }]
                textRole: "t"; valueRole: "v"
                currentIndex: Math.max(0, ["in", "out", "reset"].indexOf(ed.action.direction || "in"))
                onActivated: ed.setField("direction", currentValue)
            }
            ComboBox {
                model: [{ v: "keys", t: qsTr("Ctrl + / Ctrl −") }, { v: "ctrlScroll", t: qsTr("Ctrl + scroll") }]
                textRole: "t"; valueRole: "v"
                currentIndex: ed.action.method === "ctrlScroll" ? 1 : 0
                onActivated: ed.setField("method", currentValue)
            }
        }
    }
    Component {
        id: volumeForm
        RowLayout {
            Label { text: qsTr("Change per step") }
            SpinBox {
                from: -25; to: 25; editable: true
                value: ed.action.delta !== undefined ? ed.action.delta : 2
                onValueModified: ed.setField("delta", value)
                textFromValue: (v) => (v > 0 ? "+" : "") + v + "%"
                valueFromText: (t) => parseInt(t)
            }
            ComboBox {
                model: [{ v: "sink", t: qsTr("Speakers") }, { v: "source", t: qsTr("Microphone") }]
                textRole: "t"; valueRole: "v"
                currentIndex: ed.action.target === "source" ? 1 : 0
                onActivated: ed.setField("target", currentValue)
            }
            Label { text: qsTr("Max") }
            SpinBox {
                from: 10; to: 150; stepSize: 5
                value: ed.action.max || 100
                onValueModified: ed.setField("max", value)
            }
        }
    }
    Component {
        id: muteForm
        ComboBox {
            model: [{ v: "sink", t: qsTr("Speakers") }, { v: "source", t: qsTr("Microphone") }]
            textRole: "t"; valueRole: "v"
            currentIndex: ed.action.target === "source" ? 1 : 0
            onActivated: ed.setField("target", currentValue)
        }
    }
    Component {
        id: mediaForm
        RowLayout {
            ComboBox {
                id: mc
                Layout.preferredWidth: 200
                model: [{ v: "playpause", t: qsTr("Play / pause") }, { v: "play", t: qsTr("Play") }, { v: "pause", t: qsTr("Pause") },
                        { v: "stop", t: qsTr("Stop") }, { v: "next", t: qsTr("Next track") }, { v: "previous", t: qsTr("Previous track") },
                        { v: "seek", t: qsTr("Seek") }, { v: "rate", t: qsTr("Playback speed") }, { v: "volume", t: qsTr("Player volume") }]
                textRole: "t"; valueRole: "v"
                currentIndex: Math.max(0, ["playpause", "play", "pause", "stop", "next", "previous", "seek", "rate", "volume"].indexOf(ed.action.command || "playpause"))
                onActivated: ed.setField("command", currentValue)
            }
            SpinBox {
                visible: ed.action.command === "seek"
                from: -60000; to: 60000; stepSize: 1000; editable: true
                value: ed.action.offsetMs || 5000
                onValueModified: ed.setField("offsetMs", value)
                textFromValue: (v) => (v / 1000).toFixed(0) + " s"
                valueFromText: (t) => parseFloat(t) * 1000
            }
            SpinBox {
                visible: ed.action.command === "rate" || ed.action.command === "volume"
                from: -50; to: 50; editable: true
                value: Math.round((ed.action.rateDelta || 0.1) * 100)
                onValueModified: ed.setField("rateDelta", value / 100)
                textFromValue: (v) => (v / 100).toFixed(2)
                valueFromText: (t) => Math.round(parseFloat(t) * 100)
            }
        }
    }
    Component {
        id: deltaForm
        RowLayout {
            Label { text: qsTr("Change per step") }
            SpinBox {
                from: -50; to: 50; editable: true
                value: ed.action.delta !== undefined ? ed.action.delta : 5
                onValueModified: ed.setField("delta", value)
                textFromValue: (v) => (v > 0 ? "+" : "") + v + "%"
                valueFromText: (t) => parseInt(t)
            }
        }
    }
    Component {
        id: workspaceForm
        ComboBox {
            model: [{ v: "next", t: qsTr("Next workspace") }, { v: "previous", t: qsTr("Previous workspace") }]
            textRole: "t"; valueRole: "v"
            currentIndex: ed.action.direction === "previous" ? 1 : 0
            onActivated: ed.setField("direction", currentValue)
        }
    }
    Component {
        id: toolForm
        RowLayout {
            ComboBox {
                model: [{ v: "next", t: qsTr("Next tool") }, { v: "previous", t: qsTr("Previous tool") }, { v: "select", t: qsTr("Select tool") }]
                textRole: "t"; valueRole: "v"
                currentIndex: Math.max(0, ["next", "previous", "select"].indexOf(ed.action.op || "next"))
                onActivated: ed.setField("op", currentValue)
            }
            TextField {
                visible: ed.action.op === "select"
                placeholderText: qsTr("tool id")
                text: ed.action.id || ""
                onEditingFinished: ed.setField("id", text)
            }
        }
    }
    Component {
        id: profileForm
        RowLayout {
            ComboBox {
                model: [{ v: "cycle", t: qsTr("Cycle profiles") }, { v: "lock", t: qsTr("Lock profile") }, { v: "unlock", t: qsTr("Unlock profile") }]
                textRole: "t"; valueRole: "v"
                currentIndex: Math.max(0, ["cycle", "lock", "unlock"].indexOf(ed.action.op || "cycle"))
                onActivated: ed.setField("op", currentValue)
            }
            TextField {
                visible: ed.action.op === "lock"
                placeholderText: qsTr("profile id (empty = current)")
                text: ed.action.id || ""
                onEditingFinished: ed.setField("id", text)
            }
        }
    }
    Component {
        id: menuForm
        ComboBox {
            model: [{ v: "toggle", t: qsTr("Toggle menu") }, { v: "open", t: qsTr("Open menu") }, { v: "close", t: qsTr("Close menu") }]
            textRole: "t"; valueRole: "v"
            currentIndex: Math.max(0, ["toggle", "open", "close"].indexOf(ed.action.op || "toggle"))
            onActivated: ed.setField("op", currentValue)
        }
    }
    Component {
        id: hapticForm
        RowLayout {
            ComboBox {
                model: [{ v: "click", t: qsTr("Click") }, { v: "buzz", t: qsTr("Buzz") }, { v: "rumble", t: qsTr("Rumble") }]
                textRole: "t"; valueRole: "v"
                currentIndex: Math.max(0, ["click", "buzz", "rumble"].indexOf(ed.action.waveform || "click"))
                onActivated: ed.setField("waveform", currentValue)
            }
            Label { text: qsTr("Repeat") }
            SpinBox { from: 1; to: 10; value: ed.action.repeat || 1; onValueModified: ed.setField("repeat", value) }
        }
    }
    Component {
        id: osdForm
        TextField { placeholderText: qsTr("Message"); text: ed.action.text || ""; onEditingFinished: ed.setField("text", text) }
    }
    Component {
        id: pluginForm
        RowLayout {
            TextField { placeholderText: qsTr("plugin app id"); text: ed.action.appId || ""; onEditingFinished: ed.setField("appId", text) }
            TextField { placeholderText: qsTr("tool id"); text: ed.action.toolId || ""; onEditingFinished: ed.setField("toolId", text) }
        }
    }
    Component {
        id: valueForm
        ColumnLayout {
            GridLayout {
                columns: 4
                Label { text: qsTr("Variable") }
                TextField { text: ed.action.variable || ""; onEditingFinished: ed.setField("variable", text) }
                Label { text: qsTr("Label") }
                TextField { text: ed.action.label || ""; placeholderText: ed.action.variable || ""; onEditingFinished: ed.setField("label", text) }
                Label { text: qsTr("Step") }
                TextField { text: String(ed.action.step !== undefined ? ed.action.step : 1); validator: DoubleValidator {} onEditingFinished: ed.setField("step", Number(text)) }
                Label { text: qsTr("Default") }
                TextField { text: String(ed.action["default"] !== undefined ? ed.action["default"] : 0); validator: DoubleValidator {} onEditingFinished: ed.setField("default", Number(text)) }
                Label { text: qsTr("Minimum") }
                TextField { text: String(ed.action.min !== undefined ? ed.action.min : 0); validator: DoubleValidator {} onEditingFinished: ed.setField("min", Number(text)) }
                Label { text: qsTr("Maximum") }
                TextField { text: String(ed.action.max !== undefined ? ed.action.max : 100); validator: DoubleValidator {} onEditingFinished: ed.setField("max", Number(text)) }
            }
            CheckBox { text: qsTr("Wrap around at the ends"); checked: ed.action.wrap === true; onToggled: ed.setField("wrap", checked) }
            Label { text: qsTr("Then send (use {value} in fields):"); opacity: 0.75; visible: ed.depth < 3 }
            Loader {
                Layout.fillWidth: true
                active: ed.depth < 3
                source: "ActionEditor.qml"
                onLoaded: { item.path = ed.path + ".output"; item.depth = ed.depth + 1 }
            }
        }
    }
    Component {
        id: oscForm
        GridLayout {
            columns: 4
            Label { text: qsTr("Host") }
            TextField { text: ed.action.host || "127.0.0.1"; onEditingFinished: ed.setField("host", text) }
            Label { text: qsTr("Port") }
            SpinBox { from: 1; to: 65535; editable: true; value: ed.action.port || 9000; onValueModified: ed.setField("port", value) }
            Label { text: qsTr("Address") }
            TextField { text: ed.action.address || "/"; onEditingFinished: ed.setField("address", text) }
            Label { text: qsTr("Arguments") }
            TextField {
                text: (ed.action.args || []).join(" ")
                placeholderText: "{value} 1 0.5 name"
                onEditingFinished: ed.setField("args", text.split(" ").filter((s) => s.length).map((s) => isNaN(Number(s)) ? s : Number(s)))
            }
        }
    }
    Component {
        id: launchForm
        RowLayout {
            ComboBox {
                id: lk
                model: [{ v: "url", t: qsTr("URL") }, { v: "path", t: qsTr("File or folder") }, { v: "desktopId", t: qsTr("Application") }]
                textRole: "t"; valueRole: "v"
                currentIndex: ed.action.desktopId !== undefined ? 2 : ed.action.path !== undefined ? 1 : 0
                onActivated: { const o = { type: "launch" }; o[currentValue] = ""; backend.set(ed.path, o) }
            }
            TextField {
                Layout.fillWidth: true
                readonly property string key: ed.action.desktopId !== undefined ? "desktopId" : ed.action.path !== undefined ? "path" : "url"
                text: ed.action[key] || ""
                placeholderText: key === "desktopId" ? "org.kde.krita" : key === "path" ? "/home/me/Documents" : "https://"
                onEditingFinished: ed.setField(key, text)
            }
        }
    }
    Component {
        id: commandForm
        ColumnLayout {
            TextField {
                Layout.fillWidth: true
                font.family: "monospace"
                text: JSON.stringify(ed.action.argv || [])
                onEditingFinished: { try { ed.setField("argv", JSON.parse(text)) } catch (e) { } }
                ToolTip.visible: activeFocus
                ToolTip.text: qsTr("Arguments as a JSON list, no shell: [\"notify-send\", \"Hello\"]")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: qsTr("Programs run without a shell. OpenDial asks you to approve this action before it first runs.")
            }
        }
    }
    Component {
        id: macroForm
        ColumnLayout {
            Repeater {
                model: ed.action.steps || []
                delegate: Pane {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    padding: 8
                    background: Rectangle { radius: 8; color: "transparent"; border.color: Qt.rgba(0.5, 0.5, 0.5, 0.3) }
                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        RowLayout {
                            Label { text: qsTr("Step %1").arg(index + 1); font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            ToolButton { icon.source: "image://opendial/up?color=888888"; enabled: index > 0; onClicked: backend.move(ed.path + ".steps", index, index - 1) }
                            ToolButton { icon.source: "image://opendial/down?color=888888"; enabled: index < (ed.action.steps || []).length - 1; onClicked: backend.move(ed.path + ".steps", index, index + 1) }
                            ToolButton { icon.source: "image://opendial/delete?color=888888"; onClicked: backend.remove(ed.path + ".steps." + index) }
                        }
                        RowLayout {
                            visible: modelData.delayMs !== undefined
                            Label { text: qsTr("Wait") }
                            SpinBox { from: 0; to: 10000; stepSize: 50; editable: true; value: modelData.delayMs || 0; onValueModified: backend.set(ed.path + ".steps." + index + ".delayMs", value) }
                            Label { text: qsTr("ms") }
                        }
                        Loader {
                            Layout.fillWidth: true
                            active: modelData.action !== undefined && ed.depth < 3
                            source: "ActionEditor.qml"
                            onLoaded: { item.path = ed.path + ".steps." + index + ".action"; item.depth = ed.depth + 1; item.allowNone = false }
                        }
                    }
                }
            }
            RowLayout {
                Button { text: qsTr("Add action"); onClicked: backend.insert(ed.path + ".steps", -1, { action: { type: "key", keys: "" } }) }
                Button { text: qsTr("Add delay"); onClicked: backend.insert(ed.path + ".steps", -1, { delayMs: 100 }) }
                Label { text: qsTr("Repeat") }
                SpinBox { from: 1; to: 20; value: ed.action.repeat || 1; onValueModified: ed.setField("repeat", value) }
            }
        }
    }
}
