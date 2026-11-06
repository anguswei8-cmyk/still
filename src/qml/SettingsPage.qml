import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ScrollView {
    id: root
    objectName: "SettingsPage"
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    Binding { target: root.contentItem; property: "boundsBehavior"; value: Flickable.StopAtBounds }
    Connections {
        target: backend
        function onPageChanged() { if (root.contentItem && root.contentItem.contentY !== undefined) root.contentItem.contentY = 0; }
    }
    ColumnLayout {
        width: root.availableWidth - 8
        spacing: 0
        Label {
            visible: backend.page === 2
            text: qsTranslate("Still", "点击组合键后重新录入；按退格键可清空。")
            color: Theme.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.bottomMargin: 16
        }
        StartupRow { visible: backend.page === 1; Layout.fillWidth: true }
        Repeater {
            model: backend.settingRows
            delegate: SettingRow {
                required property int index
                required property var modelData
                field: modelData
                first: index === 0
                Layout.fillWidth: true
            }
        }
        Label {
            visible: backend.page === 4 || (backend.page === 5 && Qt.platform.os === "osx")
            text: backend.page === 4 ? qsTranslate("Still", "贴图会自动识别文字。拖选后复制，按住 Alt 拖动可直接移动贴图。") : qsTranslate("Still", "自动粘贴需要系统辅助功能权限；未授权时仍可复制，再按 Command+V。")
            font.pixelSize: 12
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.topMargin: 18
        }
        UiButton {
            objectName: "ApiSettings"
            visible: backend.page === 1
            text: qsTranslate("Still", "API 文字校对设置…")
            Layout.topMargin: 18
            onClicked: backend.openApiSettings()
        }
        UiButton {
            objectName: "SettingsBackup"
            visible: backend.page === 1
            text: qsTranslate("Still", "配置备份与恢复…")
            Layout.topMargin: 12
            onClicked: backend.openSettingsBackup()
        }
        Item { height: 12 }
    }
}
