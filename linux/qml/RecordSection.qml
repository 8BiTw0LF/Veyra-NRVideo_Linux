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
        width: bText.implicitWidth + 20; height: 32; radius: 10
        color: danger ? (bMa.containsMouse ? "#ff6b6b" : "#e04848")
             : (bMa.containsMouse ? Theme.surfaceHover : Theme.surface)
        gradient: primary && !danger ? (bMa.containsMouse ? Theme.sunsetHover : Theme.sunset) : null
        Text { id: bText; anchors.centerIn: parent; text: b.text; color: b.primary || b.danger ? Theme.accentInk : Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
        MouseArea { id: bMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: b.clicked() }
    }

    Item {
        width: root.width; height: 28
        Text { text: tr("recTitle"); color: Theme.heading; font.pixelSize: 18; font.weight: Font.Bold; font.letterSpacing: 0.5; anchors.verticalCenter: parent.verticalCenter }
        ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            checked: player.replay; onToggled: (v) => player.replay = v }
    }
    Text {
        width: root.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 13
        color: player.recording ? "#ff8a8a" : Theme.muted
        text: player.recording || player.recInfo !== "" ? player.recInfo + (player.replay && !player.recording ? "  ·  " + tr("recReplayOn").replace("%1", player.replaySeconds) : "")
              : player.replay ? tr("recReplayOn").replace("%1", player.replaySeconds) : tr("recOff")
    }
    Text { text: tr("recLength"); color: Theme.label; font.pixelSize: 14 }
    Segmented { width: root.width; model: ["30 s", "60 s", "120 s"]
        current: Math.max(0, root.lengths.indexOf(player.replaySeconds)); onPicked: (i) => player.replaySeconds = root.lengths[i] }
    Text { text: tr("recRes"); color: Theme.label; font.pixelSize: 14 }
    Segmented { width: root.width; model: ["720p", "1080p", "1440p", tr("native")]
        current: Math.max(0, root.heights.indexOf(player.recHeight)); onPicked: (i) => player.recHeight = root.heights[i] }
    Flow {
        width: root.width; spacing: 8
        Btn { text: tr("recSave"); primary: player.replay; onClicked: player.saveReplay() }
        Btn { text: player.recording ? tr("recStop") : tr("recStart"); danger: player.recording; onClicked: player.toggleRecording() }
        Btn { text: tr("recShot"); onClicked: player.screenshot() }
        Btn { text: tr("recFolder"); onClicked: player.openRecordingsFolder() }
    }
}
