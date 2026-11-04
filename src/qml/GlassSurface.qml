import QtQuick

Rectangle {
    id: glass
    property color tint: Theme.glass
    property bool emphasized: false
    radius: 18
    color: tint
    gradient: Theme.dark ? null : lightGradient
    Gradient {
        id: lightGradient
        GradientStop { position: 0; color: Qt.lighter(glass.tint, glass.emphasized ? 1.10 : 1.03) }
        GradientStop { position: 0.5; color: glass.tint }
        GradientStop { position: 1; color: Qt.darker(glass.tint, 1.04) }
    }
    border.width: 1
    border.color: Theme.glassRim
}
