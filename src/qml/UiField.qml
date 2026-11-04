import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    implicitHeight: 36
    leftPadding: 11
    rightPadding: 11
    topPadding: 7
    bottomPadding: 7
    font.family: Theme.fontFamily
    font.pixelSize: 13
    color: Theme.ink
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: "white"
    selectByMouse: true
    background: Rectangle {
        color: control.enabled ? Theme.surface : Theme.canvas
        radius: 9
        border.color: control.activeFocus ? Theme.focus : "transparent"
        border.width: 1
    }
}
