import QtQuick

// OBS browser source: a local stream of the finished picture + an HTML file to add in OBS as a Browser source.
Column {
    id: root
    property var tr: function (k) { return k }
    spacing: 10

    readonly property var heights: [720, 1080, 1440, 0]

    component LinkButton: Rectangle {
        id: lb
        property string text
        property bool primary: true
        signal clicked()
        width: lbText.implicitWidth + 22; height: 32; radius: 10
        color: lbMa.containsMouse ? Theme.surfaceHover : Theme.surface; gradient: primary ? (lbMa.containsMouse ? Theme.sunsetHover : Theme.sunset) : null
        Text { id: lbText; anchors.centerIn: parent; text: lb.text; color: lb.primary ? Theme.accentInk : Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
        MouseArea { id: lbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: lb.clicked() }
    }

    Item {
        width: root.width; height: 28
        Text { text: tr("obs"); color: Theme.heading; font.pixelSize: 21; font.weight: Font.Bold; font.letterSpacing: 0.5; anchors.verticalCenter: parent.verticalCenter }
        ToggleSwitch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            checked: player.obsEnabled; onToggled: (v) => player.obsEnabled = v }
    }
    Text {
        width: root.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15
        color: player.obsError !== "" ? Theme.warn : (player.obsViewers > 0 ? Theme.ok : Theme.muted)
        text: player.obsError !== "" ? player.obsError
              : !player.obsEnabled ? tr("obsOff")
              : player.obsViewers > 0 ? tr("obsLive").replace("%1", player.obsViewers)
              : tr("obsWaiting").replace("%1", player.obsUrl)
    }
    Text { text: tr("obsRes"); color: Theme.label; font.pixelSize: 16 }
    Segmented {
        width: root.width
        model: ["720p", "1080p", "1440p", tr("native")]
        current: Math.max(0, root.heights.indexOf(player.obsHeight))
        onPicked: (i) => player.obsHeight = root.heights[i]
    }
    Row {
        spacing: 8
        LinkButton { text: tr("obsSave"); onClicked: player.saveObsFile() }
        LinkButton { text: tr("obsCopy"); primary: false; onClicked: player.copyObsUrl() }
    }
    Text {
        width: root.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.text
        visible: player.obsFile !== ""
        text: tr("obsSaved").replace("%1", player.obsFile)
    }
    Text { width: root.width; wrapMode: Text.Wrap; lineHeight: 1.15; font.pixelSize: 15; color: Theme.muted; text: tr("obsHelp") }
}
