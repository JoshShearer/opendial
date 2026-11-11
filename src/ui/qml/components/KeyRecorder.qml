// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

// Text field for shortcuts with a "Record" mode that captures the next chord.
RowLayout {
    id: kr
    property string keys: ""
    signal edited(string keys)
    property bool recording: false
    spacing: 6
    TextField {
        id: field
        Layout.fillWidth: true
        placeholderText: qsTr("e.g. Ctrl+Z or Ctrl+K Ctrl+C")
        text: kr.recording ? qsTr("Press a shortcut…") : kr.keys
        readOnly: kr.recording
        onEditingFinished: if (!kr.recording && text !== kr.keys) kr.edited(text)
        color: backend.validateKeys(text) === "" || kr.recording ? Material.foreground : "#d04040"
        ToolTip.visible: !kr.recording && activeFocus && backend.validateKeys(text) !== ""
        ToolTip.text: backend.validateKeys(text)
        Keys.onPressed: (event) => {
            if (!kr.recording) return
            const chord = backend.chordFromKeyEvent(event.key, event.modifiers, event.nativeScanCode)
            event.accepted = true
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) { kr.recording = false; return }
            if (chord.length === 0) return
            kr.recording = false
            kr.edited(chord)
        }
    }
    Button {
        text: kr.recording ? qsTr("Cancel") : qsTr("Record")
        flat: true
        onClicked: { kr.recording = !kr.recording; field.forceActiveFocus() }
    }
}
