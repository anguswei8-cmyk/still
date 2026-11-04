import QtQuick
import QtQuick.Controls.Basic

Switch {
    id: control
    implicitWidth: 40
    implicitHeight: 30
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    indicator: Rectangle {
        x: 1
        y: (control.height - height) / 2
        width: 38
        height: 24
        radius: 12
        color: control.checked ? Theme.accent : Theme.dark ? "#404040" : "#8b94a3"
        border.color: control.activeFocus ? Theme.focus : "transparent"
        border.width: control.activeFocus ? 2 : 0
        Rectangle {
            x: control.checked ? 17 : 3
            y: 3
            width: 18
            height: 18
            radius: 9
            color: "white"
        }
    }
    background: Rectangle {
        color: "transparent"
        radius: 14
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.focus
    }
    contentItem: Item {}
}
