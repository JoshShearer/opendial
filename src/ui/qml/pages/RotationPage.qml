// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "../components"

ScrollView {
    id: view
    contentWidth: availableWidth
    property var samples: []         // {t, deg}
    property real calibTotal: 0
    property bool calibrating: false
    readonly property real unitsPerTurnNow: { backend.config; return backend.get("settings.device.unitsPerTurn") || 0 }

    Connections {
        target: backend
        function onGesture(e) {
            if (e.kind !== "rotate") return
            const s = view.samples.slice(-119)
            s.push({ t: e.t, deg: e.degrees })
            view.samples = s
            if (view.calibrating) view.calibTotal += e.degrees
            graph.requestPaint()
        }
    }

    ColumnLayout {
        width: view.availableWidth
        spacing: 16
        Item { implicitHeight: 8 }
        Label { text: qsTr("Rotation & haptics"); font.pixelSize: 26; font.weight: Font.DemiBold; Layout.leftMargin: 24 }
        GridLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            columns: view.availableWidth > 900 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16

            Card {
                title: qsTr("Turning (all tools)")
                subtitle: qsTr("Tools can override these in the tool editor.")
                SettingSlider { label: qsTr("Turn per step"); path: "settings.rotation.detentDegrees"; from: 2; to: 90; defaultValue: 10; unit: "°" }
                SettingSlider { label: qsTr("Sensitivity"); path: "settings.rotation.sensitivity"; from: 0.25; to: 4; stepSize: 0.05; decimals: 2; defaultValue: 1; unit: "×" }
                SettingSlider { label: qsTr("Acceleration"); description: qsTr("Fast spins do more. Off keeps one action per click."); path: "settings.rotation.acceleration"; from: 0; to: 3; stepSize: 0.1; decimals: 1; defaultValue: 0 }
                SettingSlider { label: qsTr("Smoothing"); path: "settings.rotation.smoothing"; from: 0; to: 0.9; stepSize: 0.05; decimals: 2; defaultValue: 0.3 }
                SettingSwitch { label: qsTr("Reverse direction"); path: "settings.rotation.invert"; defaultValue: false }
                SettingCombo {
                    label: qsTr("Turning while pressed")
                    description: qsTr("Used when a tool has no press-and-turn actions")
                    path: "settings.gestures.pressedRotation"
                    defaultValue: "rotate"
                    options: [{ value: "rotate", text: qsTr("Same as turning") }, { value: "fine", text: qsTr("Fine adjustment") }, { value: "ignore", text: qsTr("Ignore") }]
                }
                SettingSlider { label: qsTr("Fine adjustment factor"); path: "settings.rotation.fineFactor"; from: 0.05; to: 1; stepSize: 0.05; decimals: 2; defaultValue: 0.25; unit: "×" }
            }

            Card {
                title: qsTr("Haptic feedback")
                SettingSwitch { label: qsTr("Haptics"); description: qsTr("Turn all vibration on or off"); path: "settings.haptics.enabled"; defaultValue: true }
                SettingSwitch { label: qsTr("Clicks while in the menu"); path: "settings.haptics.menuTicks"; defaultValue: true }
                SettingSwitch { label: qsTr("Pulse when selecting"); path: "settings.haptics.selectFeedback"; defaultValue: true }
                SettingSwitch { label: qsTr("Buzz at the menu edge"); description: qsTr("When wrap-around is off"); path: "settings.haptics.boundaryFeedback"; defaultValue: true }
                SettingSlider { label: qsTr("Finest haptic step"); description: qsTr("Below this, clicks are spaced out to avoid a constant buzz"); path: "settings.haptics.minDetentDegrees"; from: 1; to: 15; defaultValue: 2; unit: "°" }
                RowLayout {
                    enabled: (backend.state.haptics || {}).available === true
                    Button { text: qsTr("Click"); onClicked: backend.testHaptics("click") }
                    Button { text: qsTr("Buzz"); onClicked: backend.testHaptics("buzz") }
                    Button { text: qsTr("Rumble"); onClicked: backend.testHaptics("rumble") }
                }
                Label {
                    readonly property var h: backend.state.haptics || ({})
                    text: h.available ? qsTr("Device resolution %1 steps/turn%2").arg(h.appliedResolution).arg(h.appliedAuto ? qsTr(", detent clicks on") : "")
                                      : qsTr("Haptics unavailable: %1").arg(h.error || "")
                    opacity: 0.65
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Card {
                title: qsTr("Press timing")
                SettingSlider { label: qsTr("Hold time for press-and-hold"); path: "settings.gestures.longPressMs"; from: 250; to: 1500; stepSize: 25; defaultValue: 500; unit: " ms" }
                SettingSlider { label: qsTr("Double-click speed"); path: "settings.gestures.doubleClickMs"; from: 150; to: 800; stepSize: 10; defaultValue: 300; unit: " ms" }
                SettingSlider { label: qsTr("Turn before a press becomes press-and-turn"); path: "settings.gestures.holdRotateThresholdDeg"; from: 0; to: 20; defaultValue: 3; unit: "°" }
                SettingSlider { label: qsTr("Debounce"); path: "settings.gestures.debounceMs"; from: 0; to: 50; defaultValue: 8; unit: " ms" }
                SettingSwitch { label: qsTr("Click to latch press"); description: qsTr("Accessibility: a click acts like holding the button until the next click"); path: "settings.gestures.clickToggleLatch"; defaultValue: false }
            }

            Card {
                title: qsTr("Live rotation")
                subtitle: qsTr("Turn the Dial to see the signal.")
                Canvas {
                    id: graph
                    Layout.fillWidth: true
                    Layout.preferredHeight: 140
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = Qt.rgba(0.5, 0.5, 0.5, 0.4)
                        ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                        const s = view.samples
                        if (!s.length) return
                        let max = 5
                        for (const p of s) max = Math.max(max, Math.abs(p.deg))
                        const w = width / 120
                        for (let i = 0; i < s.length; ++i) {
                            const h = (s[i].deg / max) * (height / 2 - 4)
                            ctx.fillStyle = s[i].deg >= 0 ? Material.accent : "#e0527a"
                            ctx.fillRect(i * w, height / 2 - Math.max(h, 0), Math.max(1, w - 1), Math.abs(h))
                        }
                    }
                }
                Label { text: qsTr("Bars above the line: clockwise. Height: degrees per event."); opacity: 0.6; font.pixelSize: 12 }
            }

            Card {
                title: qsTr("Calibrate rotation")
                subtitle: (backend.state.haptics || {}).available ? qsTr("Not needed: OpenDial controls the Dial's resolution directly while haptics are available.") : qsTr("Check that one full turn of the Dial reads as 360°. Needed only without haptics access, if steps feel too big or too small.")
                RowLayout {
                    Button {
                        text: view.calibrating ? qsTr("Done: I turned exactly once") : qsTr("Start")
                        enabled: backend.daemonRunning
                        onClicked: {
                            if (!view.calibrating) { view.calibTotal = 0; view.calibrating = true; return }
                            view.calibrating = false
                            const measured = Math.abs(view.calibTotal)
                            if (measured < 30) { calibResult.text = qsTr("Too little rotation measured. Try again."); return }
                            const cur = view.unitsPerTurnNow > 0 ? view.unitsPerTurnNow : 3600
                            const units = Math.round(cur * measured / 360)
                            calibResult.text = qsTr("Measured %1° for one turn.").arg(measured.toFixed(1))
                            calibResult.suggested = units
                        }
                    }
                    Label { visible: view.calibrating; text: qsTr("Mark a point, turn one full revolution clockwise… %1°").arg(view.calibTotal.toFixed(1)) }
                }
                RowLayout {
                    Label { id: calibResult; property int suggested: 0; text: ""; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Button {
                        visible: calibResult.suggested > 0 && Math.abs(calibResult.suggested - (view.unitsPerTurnNow || 3600)) > 18
                        text: qsTr("Apply correction")
                        onClicked: { backend.set("settings.device.unitsPerTurn", calibResult.suggested); calibResult.suggested = 0; calibResult.text = qsTr("Saved.") }
                    }
                    Button {
                        visible: view.unitsPerTurnNow > 0
                        text: qsTr("Reset")
                        onClicked: backend.remove("settings.device.unitsPerTurn")
                    }
                }
            }
        }
        Item { implicitHeight: 24 }
    }
}
