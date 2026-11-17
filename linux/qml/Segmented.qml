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
            height: 38; radius: 11 * Theme.round
            color: ma.containsMouse && root.enabled ? Theme.surfaceHover : Theme.surface
            gradient: index === root.current ? Theme.sunset : null
            border.width: Theme.edge; border.color: "#000000"
            // retro: an 8-bit button - light top edge, dark bottom edge (pressed in when selected)
            Rectangle { visible: Theme.edge > 0; x: Theme.edge; y: Theme.edge; width: parent.width - 2 * Theme.edge; height: 2
                color: index === root.current ? "#40000000" : "#40ffffff" }
            Rectangle { visible: Theme.edge > 0; x: Theme.edge; y: parent.height - Theme.edge - 3; width: parent.width - 2 * Theme.edge; height: 3
                color: index === root.current ? "#30ffffff" : "#59000000" }
            Text { font.family: Theme.font; anchors.centerIn: parent; text: modelData; font.pixelSize: Theme.headCaps ? 14 : 16
                font.capitalization: Theme.headCaps ? Font.AllUppercase : Font.MixedCase
                font.weight: index === root.current || Theme.headCaps ? Font.DemiBold : Font.Medium
                color: index === root.current ? Theme.accentInk : Theme.text }
            MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; enabled: root.enabled
                cursorShape: Qt.PointingHandCursor; onClicked: root.picked(index) }
        }
    }
}
