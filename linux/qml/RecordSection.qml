import QtQuick

// Instant replay (F9), recording (F10) and screenshots (F12) of the finished picture, encoded on the GPU (VA-API).
Column {
    id: root
    property var tr: function (k) { return k }
    spacing: 10

    readonly property var lengths: [30, 60, 120]
    readonly property var heights: [720, 1080, 1440, 0]

    component Btn: Rectangle {
        id: b
        property string text
        property bool primary: false
        property bool danger: false
        signal clicked()
        width: bText.implicitWidth + 20; height: 32; radius: 10 * Theme.round
        color: danger ? (bMa.containsMouse ? "#ff6b6b" : "#e04848")
             : (bMa.containsMouse ? Theme.surfaceHover : Theme.surface)
        gradient: primary && !danger ? (bMa.containsMouse ? Theme.sunsetHover : Theme.sunset) : null
        Text { font.family: Theme.font; id: bText; anchors.centerIn: parent; text: b.text; color: b.primary || b.danger ? Theme.accentInk : Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
        MouseArea { id: bMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
    }

    Item {
        width: root.width; height: 28
        Text { font.family: Theme.headFont; width: Math.min(implicitWidth, parent.width - 62); elide: Text.ElideRight; font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase; text: tr("recTitle"); color: Theme.heading; font.pixelSize: 21 * Theme.headScale; font.weight: Font.Bold; font.letterSpacing: 0.5; anchors.verticalCenter: parent.verticalCenter }
        ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            checked: player.replay; onToggled: (v) => player.replay = v }
    }
    Text { font.family: Theme.font;
        width: root.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15
        color: player.recording ? "#ff8a8a" : Theme.muted
        text: player.recording || player.recInfo !== "" ? player.recInfo + (player.replay && !player.recording ? "  ·  " + tr("recReplayOn").replace("%1", player.replaySeconds) : "")
              : player.replay ? tr("recReplayOn").replace("%1", player.replaySeconds) : tr("recOff")
    }
    Text { font.family: Theme.font; text: tr("recLength"); color: Theme.label; font.pixelSize: 16 }
    Segmented { width: root.width; model: ["30 s", "60 s", "120 s"]
        current: Math.max(0, root.lengths.indexOf(player.replaySeconds)); onPicked: (i) => player.replaySeconds = root.lengths[i] }
    Text { font.family: Theme.font; text: tr("recRes"); color: Theme.label; font.pixelSize: 16 }
    Segmented { width: root.width; model: ["720p", "1080p", "1440p", tr("native")]
        current: Math.max(0, root.heights.indexOf(player.recHeight)); onPicked: (i) => player.recHeight = root.heights[i] }
    Flow {
        width: root.width; spacing: 8
        Btn { text: tr("recSave"); primary: player.replay; onClicked: player.saveReplay() }
        Btn { text: player.recording ? tr("recStop") : tr("recStart"); danger: player.recording; onClicked: player.toggleRecording() }
        Btn { text: tr("recShot"); onClicked: player.screenshot() }
        Btn { text: tr("recFolder"); onClicked: player.openRecordingsFolder() }
    }

    // ---- live stream (RTMP) ----
    Item { width: root.width; height: 6 }
    Text { font.family: Theme.font; text: tr("streamTitle"); color: Theme.label; font.pixelSize: 16; font.weight: Font.Medium }
    Segmented { width: root.width; model: ["Twitch", "YouTube", tr("streamOwn")]
        current: player.streamService; onPicked: (i) => player.streamService = i }
    Rectangle {
        visible: player.streamService === 2
        width: root.width; height: 32; radius: 9 * Theme.round; color: Theme.surfaceDeep; border.color: srvIn.activeFocus ? Theme.accentLine : Theme.border
        TextInput {
            id: srvIn; anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
            verticalAlignment: TextInput.AlignVCenter; color: Theme.text; font.pixelSize: 16; clip: true; selectByMouse: true
            text: player.streamServer; onEditingFinished: player.streamServer = text
            Text { font.family: Theme.font; anchors.verticalCenter: parent.verticalCenter; visible: !parent.text && !parent.activeFocus; text: "rtmp://server/app"; color: Theme.dim; font.pixelSize: 16 }
        }
    }
    Rectangle {
        width: root.width; height: 32; radius: 9 * Theme.round; color: Theme.surfaceDeep; border.color: keyIn.activeFocus ? Theme.accentLine : Theme.border
        TextInput {
            id: keyIn; anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
            verticalAlignment: TextInput.AlignVCenter; color: Theme.text; font.pixelSize: 16; clip: true; selectByMouse: true
            echoMode: TextInput.Password; text: player.streamKey; onEditingFinished: player.streamKey = text
            Text { font.family: Theme.font; anchors.verticalCenter: parent.verticalCenter; visible: !parent.text && !parent.activeFocus; text: tr("streamKey"); color: Theme.dim; font.pixelSize: 16 }
        }
    }
    Segmented { width: root.width; model: ["3500 kbps", "6000 kbps", "8000 kbps"]
        readonly property var rates: [3500, 6000, 8000]
        current: Math.max(0, rates.indexOf(player.streamKbps)); onPicked: (i) => player.streamKbps = rates[i] }
    Btn { text: player.streaming ? tr("streamStop") : tr("streamStart"); danger: player.streaming; primary: !player.streaming
        onClicked: { player.streamKey = keyIn.text; player.toggleStream() } }
    Text { font.family: Theme.font; width: root.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: tr("streamHint") }
}
