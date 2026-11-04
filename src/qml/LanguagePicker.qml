import QtQuick
import QtQuick.Layouts

Rectangle {
    implicitHeight: 38
    radius: 10
    color: Theme.hover
    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 3
        Repeater {
            model: [{ value: "zh_CN", text: qsTranslate("Still", "简体中文") }, { value: "en", text: "English" }]
            delegate: UiButton {
                required property var modelData
                objectName: "Language_" + modelData.value
                Layout.fillWidth: true
                implicitHeight: 32
                text: modelData.text
                checked: backend.form.appearance.language === modelData.value
                checkable: true
                autoExclusive: true
                Accessible.role: Accessible.RadioButton
                onClicked: backend.setSetting("appearance", "language", modelData.value)
                background: Rectangle { radius: 7; color: parent.checked ? Theme.surface : "transparent"; border.color: parent.visualFocus ? Theme.focus : "transparent" }
            }
        }
    }
}
