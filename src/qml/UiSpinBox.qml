import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: control
    implicitHeight: 36
    editable: true
    leftPadding: 12
    rightPadding: 30
    font.family: Theme.fontFamily
    font.pixelSize: 13
    opacity: enabled ? 1 : 0.45
    contentItem: TextInput {
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: Theme.ink
        selectionColor: Theme.accent
        selectedTextColor: "white"
        verticalAlignment: Text.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
    }
    up.indicator: Rectangle {
        x: control.width - width - 3; y: 2
        width: 25; height: 16
        radius: 4
        color: control.up.pressed ? Theme.selection : control.up.hovered ? Theme.hover : "transparent"
        Image { anchors.centerIn: parent; width: 12; height: 12; source: "image://content/icon/chevron" + (Theme.dark ? "-dark" : ""); rotation: 180; opacity: control.up.enabled ? 1 : 0.35 }
    }
    down.indicator: Rectangle {
        x: control.width - width - 3; y: control.height - height - 2
        width: 25; height: 16
        radius: 4
        color: control.down.pressed ? Theme.selection : control.down.hovered ? Theme.hover : "transparent"
        Image { anchors.centerIn: parent; width: 12; height: 12; source: "image://content/icon/chevron" + (Theme.dark ? "-dark" : ""); opacity: control.down.enabled ? 1 : 0.35 }
    }
    background: Rectangle { radius: 9; color: Theme.surface; border.color: control.activeFocus ? Theme.focus : "transparent" }
}
