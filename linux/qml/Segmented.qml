import QtQuick
Row {
    id: root
    property var model: []
    property int current: 0
    property bool enabled: true
    signal picked(int index)
    spacing: 5
    opacity: enabled ? 1 : 0.4
    Repeater {
        model: root.model
        Rectangle {
            width: (root.width - (root.model.length - 1) * root.spacing) / root.model.length
            height: 38; radius: 11
            color: ma.containsMouse && root.enabled ? Theme.surfaceHover : Theme.surface
            gradient: index === root.current ? Theme.sunset : null
            Text { anchors.centerIn: parent; text: modelData; font.pixelSize: 16; font.weight: index === root.current ? Font.DemiBold : Font.Medium
                color: index === root.current ? Theme.accentInk : Theme.text }
            MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; enabled: root.enabled
                cursorShape: Qt.PointingHandCursor; onClicked: root.picked(index) }
        }
    }
}
