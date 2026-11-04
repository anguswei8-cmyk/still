import QtQuick
import QtQuick.Layouts

RowLayout {
    spacing: 8
    implicitHeight: 100
    Repeater {
        model: [{ value: "light", text: qsTranslate("Still", "浅色") }, { value: "dark", text: qsTranslate("Still", "深色") }, { value: "system", text: qsTranslate("Still", "跟随系统") }]
        delegate: UiButton {
            id: option
            required property var modelData
            objectName: "Theme_" + modelData.value
            Layout.fillWidth: true
            Layout.preferredWidth: 100
            Layout.minimumWidth: 0
            implicitHeight: 100
            leftPadding: 7; rightPadding: 7
            checked: backend.form.appearance.theme === modelData.value
            checkable: true
            autoExclusive: true
            text: modelData.text
            Accessible.name: text
            Accessible.role: Accessible.RadioButton
            onClicked: backend.setSetting("appearance", "theme", modelData.value)
            contentItem: ColumnLayout {
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    radius: 6
                    color: option.modelData.value === "dark" ? "#1c1c1c" : "#ffffff"
                    border.color: Theme.border
                    Rectangle { visible: option.modelData.value === "system"; anchors.right: parent.right; width: parent.width / 2; height: parent.height; radius: 6; color: "#1c1c1c" }
                    Rectangle { x: 4; y: 4; width: 14; height: parent.height - 8; radius: 3; color: "#858e9e" }
                    Rectangle { x: 24; y: 7; width: parent.width - 32; height: 5; radius: 2; color: "#858e9e" }
                    Row {
                        x: 24; y: 20; spacing: 4
                        Repeater { model: 2; Rectangle { width: (option.width - 54) / 2; height: 22; radius: 3; color: option.modelData.value === "dark" ? "#292929" : "#e2e6ed" } }
                    }
                }
                Text { Layout.fillWidth: true; text: option.text; textFormat: Text.PlainText; font.family: Theme.fontFamily; font.pixelSize: 12; color: Theme.ink; horizontalAlignment: Text.AlignHCenter }
            }
            background: Rectangle { radius: 10; color: option.checked ? Theme.selection : "transparent"; border.color: option.checked || option.visualFocus ? Theme.focus : "transparent" }
        }
    }
}
