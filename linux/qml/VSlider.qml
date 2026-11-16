import QtQuick
// Minimal horizontal slider (no Qt Quick Controls dependency).
Item {
    id: root
    property real from: 0
    property real to: 1
    property real value: 0
    property real neutral: NaN         // draws the fill from this value instead of 'from'
    property color accent: Theme.accent
    property bool dragging: ma.pressed
    signal moved(real v)
    height: 26
    readonly property real pos: (Math.max(from, Math.min(to, value)) - from) / (to - from)
    Rectangle { id: track; anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 5; radius: 2.5; color: Theme.surfaceHover
        Rectangle {
            readonly property real n: isNaN(root.neutral) ? 0 : (root.neutral - root.from) / (root.to - root.from)
            x: Math.min(n, root.pos) * parent.width
            width: Math.abs(root.pos - n) * parent.width
            height: parent.height; radius: 2; color: root.accent
        }
    }
    Rectangle {
        x: root.pos * (root.width - width); anchors.verticalCenter: parent.verticalCenter
        width: ma.pressed || ma.containsMouse ? 17 : 15; height: width; radius: width / 2; color: Theme.heading
    }
    MouseArea {
        id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        function upd(mx) { root.moved(root.from + Math.max(0, Math.min(1, mx / width)) * (root.to - root.from)) }
        onPressed: (m) => upd(m.x)
        onPositionChanged: (m) => { if (pressed) upd(m.x) }
        onDoubleClicked: if (!isNaN(root.neutral)) root.moved(root.neutral)
    }
}
