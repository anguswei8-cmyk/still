import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 32
    font.family: Theme.fontFamily
    font.pixelSize: 13
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    contentItem: Text {
        text: control.displayText
        textFormat: Text.PlainText
        font: control.font
        color: Theme.ink
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Image {
        x: control.width - width - 11
        y: (control.height - height) / 2
        width: 14; height: 14
        source: "image://content/icon/chevron" + (Theme.dark ? "-dark" : "")
        sourceSize: Qt.size(28, 28)
    }
    background: Rectangle {
        radius: 9
        color: control.hovered ? Theme.hover : Theme.surface
        border.color: control.visualFocus ? Theme.focus : "transparent"
    }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        width: control.width - 10
        height: 36
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: modelData[control.textRole]
            textFormat: Text.PlainText
            font: control.font
            color: Theme.ink
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { radius: 5; color: parent.highlighted ? Theme.selection : "transparent" }
    }
    popup: Popup {
        y: control.height + 5
        width: control.width
        padding: 5
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 250)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle { radius: 8; color: Theme.surface; border.color: Theme.border }
    }
}
