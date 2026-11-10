import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: control
    property bool primary: false
    property bool quiet: false
    property bool inverse: false
    property string glyph: ""
    property string keyHint: ""
    implicitWidth: Math.max(36, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    font.family: Theme.fontFamily
    font.pixelSize: 13
    hoverEnabled: true
    Accessible.name: text || ToolTip.text
    ToolTip.visible: hovered && ToolTip.text.length > 0
    ToolTip.delay: 600
    opacity: enabled ? 1 : 0.45
    contentItem: RowLayout {
        spacing: 7
        Image {
            visible: control.glyph.length > 0
            source: visible ? "image://content/icon/" + control.glyph + (control.primary || control.inverse ? "-inverse" : (control.checked ? "-active" : "") + (Theme.dark ? "-dark" : "")) : ""
            sourceSize: Qt.size(20, 20)
            Layout.preferredWidth: visible ? 18 : 0
            Layout.preferredHeight: 18
        }
        Text {
            visible: control.text.length > 0
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.primary ? Theme.primaryInk : control.inverse ? "white" : control.checked ? Theme.focus : Theme.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
        }
        Rectangle {
            visible: control.keyHint.length > 0
            implicitWidth: hint.implicitWidth + 10
            implicitHeight: 22
            radius: 4
            color: Theme.dark ? "transparent" : control.primary ? Theme.primaryPressed : Theme.canvas
            Text {
                id: hint
                anchors.centerIn: parent
                text: control.keyHint
                font.family: Theme.fontFamily
                font.pixelSize: 10
                color: control.primary && !Theme.dark ? Theme.primaryInk : Theme.muted
            }
        }
    }
    background: Rectangle {
        radius: Theme.buttonRadius
        visible: !control.quiet || control.hovered || control.down || control.checked || control.visualFocus
        color: control.inverse ? (control.hovered || control.down ? Theme.primaryHover : "transparent") : control.primary ? (control.down ? Theme.primaryPressed : control.hovered ? Theme.primaryHover : Theme.primary)
                               : control.down || control.checked ? Theme.selection : control.hovered ? Theme.hover : control.quiet ? "transparent" : Theme.surface
        border.width: 1
        border.color: control.visualFocus ? Theme.focus : control.primary || control.quiet ? "transparent" : Theme.border
    }
}
