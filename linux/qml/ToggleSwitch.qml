import QtQuick
Item {
    id: root
    property bool checked: false
    signal toggled(bool value)
    width: 44; height: 24
    Rectangle { anchors.fill: parent; radius: height / 2; color: Theme.surfaceHover; gradient: root.checked ? Theme.sunset : null
        Behavior on color { ColorAnimation { duration: 120 } }
        Rectangle { y: 3; x: root.checked ? parent.width - width - 3 : 3; width: 18; height: 18; radius: 9; color: Theme.heading
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } } }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggled(!root.checked) }
}
