import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    objectName: "StartupRow"
    spacing: 0
    Label {
        text: qsTranslate("Still", "启动")
        font.pixelSize: 13
        font.weight: Font.DemiBold
        color: Theme.ink
        Layout.bottomMargin: 10
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 76
        spacing: 22
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: qsTranslate("Still", "登录后启动")
                textFormat: Text.PlainText
                font.pixelSize: 14
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTranslate("Still", "登录当前账户后自动打开 Still")
                textFormat: Text.PlainText
                font.pixelSize: 12
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        UiSwitch {
            objectName: "StartupEnabled"
            checked: backend.startup.enabled
            enabled: backend.startup.canToggle
            Accessible.name: qsTranslate("Still", "登录后启动")
            Accessible.description: qsTranslate("Still", "登录当前账户后自动打开 Still")
            onToggled: backend.setStartupEnabled(checked)
        }
        Item { Layout.preferredWidth: 36 }
    }
    Label {
        objectName: "StartupStatus"
        visible: text.length > 0
        text: backend.startup.error || backend.startup.message
        textFormat: Text.PlainText
        font.pixelSize: 12
        color: backend.startup.error ? Theme.error : Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Accessible.name: text
    }
    RowLayout {
        visible: backend.startup.showSettings
        Layout.topMargin: 10
        spacing: 8
        UiButton {
            objectName: "StartupSystemSettings"
            text: backend.startup.settingsLabel
            onClicked: backend.openStartupSettings()
        }
        UiButton {
            objectName: "StartupRemove"
            visible: backend.startup.needsAttention
            text: qsTranslate("Still", "取消登记")
            quiet: true
            onClicked: backend.setStartupEnabled(false)
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.sectionTopSpacing
        Layout.bottomMargin: Theme.sectionBottomSpacing
        height: 1
        color: Qt.rgba(Theme.border.r, Theme.border.g, Theme.border.b, 0.5)
    }
}
