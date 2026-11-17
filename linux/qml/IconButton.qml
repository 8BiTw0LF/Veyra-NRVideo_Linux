import QtQuick
Item {
    id: root
    property url source
    property int size: 40
    property bool active: false
    signal clicked()
    width: size; height: size
    Rectangle {
        anchors.fill: parent; radius: width / 2 * Theme.round
        color: root.active ? Theme.accentSoft : (ma.containsMouse ? Theme.border : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    Image {
        anchors.centerIn: parent
        source: root.source
        width: root.size * 0.5; height: width
        sourceSize: Qt.size(width * 2, height * 2)
        opacity: root.active ? 1 : 0.92
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }
}
