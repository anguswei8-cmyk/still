import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property real captureClearance: 0
    spacing: 16
    readonly property string selectedPath: list.currentIndex >= 0 && list.currentIndex < backend.historyItems.length ? backend.historyItems[list.currentIndex].path : ""
    RowLayout {
        Layout.fillWidth: true
        Label { text: backend.historySummary; textFormat: Text.PlainText; font.pixelSize: 12; color: Theme.muted; Layout.fillWidth: true }
        UiButton { objectName: "HistoryStorage"; glyph: "settings"; text: qsTranslate("Still", "存储设置"); quiet: true; onClicked: storage.open() }
    }
    GridView {
        id: list
        objectName: "HistoryList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        cellWidth: width / Math.max(1, Math.floor(width / 224))
        cellHeight: 198
        model: backend.historyItems
        clip: true
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        Accessible.name: qsTranslate("Still", "截图历史，双击继续编辑")
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle {
            id: card
            required property int index
            required property var modelData
            width: list.cellWidth - 14
            height: list.cellHeight - 14
            radius: 12
            color: Theme.surface
            border.width: list.currentIndex === index ? 2 : 1
            border.color: list.currentIndex === index ? Theme.focus : Theme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Image { source: card.modelData.image; fillMode: Image.PreserveAspectFit; Layout.fillWidth: true; Layout.fillHeight: true }
                Label { text: card.modelData.title; textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 13; color: Theme.ink }
                Label { text: card.modelData.date; color: Theme.muted; font.pixelSize: 11 }
            }
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.title
            MouseArea { anchors.fill: parent; onClicked: { list.currentIndex = card.index; list.forceActiveFocus(); } onDoubleClicked: backend.historyAction(card.modelData.path, "edit") }
        }
        ColumnLayout {
            visible: backend.historyItems.length === 0 && !backend.historyLoading
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 340)
            spacing: 16
            Image { source: "image://content/icon/history" + (Theme.dark ? "-dark" : ""); sourceSize: Qt.size(88, 88); Layout.preferredWidth: 44; Layout.preferredHeight: 44; Layout.alignment: Qt.AlignHCenter }
            Label { text: qsTranslate("Still", "还没有截图历史\n复制、保存或贴图后的截图，会保留在这里。"); color: Theme.muted; font.pixelSize: 14; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
            UiButton { text: qsTranslate("Still", "截取第一张图片"); primary: true; Layout.alignment: Qt.AlignHCenter; onClicked: backend.captureRequested() }
        }
    }
    UiButton { visible: backend.historyMore; text: backend.historyLoading ? qsTranslate("Still", "正在读取…") : qsTranslate("Still", "加载更多"); enabled: !backend.historyLoading; Layout.alignment: Qt.AlignHCenter; onClicked: backend.loadMoreHistory() }
    RowLayout {
        objectName: "HistoryActions"
        Layout.fillWidth: true
        Layout.bottomMargin: root.captureClearance
        spacing: 8
        UiButton { text: qsTranslate("Still", "贴图"); visible: backend.historyItems.length > 0; enabled: root.selectedPath.length > 0 && !backend.busy; onClicked: backend.historyAction(root.selectedPath, "pin") }
        UiButton { text: qsTranslate("Still", "删除选中"); visible: backend.historyItems.length > 0; enabled: root.selectedPath.length > 0 && !backend.busy; onClicked: backend.historyAction(root.selectedPath, "remove") }
        Item { Layout.fillWidth: true }
        UiButton { text: qsTranslate("Still", "清空历史"); visible: backend.historyItems.length > 0; enabled: !backend.busy; quiet: true; onClicked: backend.clearHistory() }
        UiButton { text: qsTranslate("Still", "打开目录"); quiet: true; onClicked: backend.openDirectory("history") }
        UiButton { text: qsTranslate("Still", "继续编辑"); visible: backend.historyItems.length > 0; primary: true; enabled: root.selectedPath.length > 0 && !backend.busy; onClicked: backend.historyAction(root.selectedPath, "edit") }
    }
    Popup {
        id: storage
        objectName: "HistoryStoragePopup"
        x: root.width - width
        y: 0
        width: Math.min(580, root.width)
        height: Math.min(560, root.height)
        padding: 22
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToHide: backend.commitDrafts()
        background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                objectName: "HistoryStorageStatus"
                text: backend.status
                textFormat: Text.PlainText
                visible: text.length > 0
                Layout.fillWidth: true
                font.pixelSize: 12
                color: backend.statusError ? Theme.error : Theme.muted
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
                Accessible.name: qsTranslate("Still", "设置保存状态")
            }
            Loader { active: storage.visible; sourceComponent: SettingsPage {} Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
}
