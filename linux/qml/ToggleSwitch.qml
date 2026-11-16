import QtQuick
Item {
    id: root
    property bool checked: false
    signal toggled(bool value)
    width: 50; height: 28
    Rectangle { anchors.fill: parent; radius: height / 2; color: Theme.surfaceHover; gradient: root.checked ? Theme.sunset : null
        Behavior on color { ColorAnimation { duration: 120 } }
        Rectangle { y: 3; x: root.checked ? parent.width - width - 3 : 3; width: 22; height: 22; radius: 11; color: Theme.heading
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } } }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggled(!root.checked) }
}
