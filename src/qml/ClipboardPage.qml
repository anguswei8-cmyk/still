import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "ClipboardPanel"
    property real captureClearance: 0
    property var searchField
    property double clock: Date.now()
    readonly property bool previewFits: width >= 660
    readonly property bool canPin: ["image", "text", "rich", "link"].indexOf(backend.selected.kind) >= 0
    readonly property string pinLabel: backend.selected.kind === "image" ? qsTranslate("Still", "图片贴图") : backend.selected.kind === "rich" ? qsTranslate("Still", "富文本贴图") : qsTranslate("Still", "文字贴图")
    onPreviewFitsChanged: backend.setClipboardPreviewFits(previewFits)
    Component.onCompleted: backend.setClipboardPreviewFits(previewFits)
    spacing: 10
    component RowAction: UiButton {
        width: 28; height: 28
        padding: 0
        leftPadding: 5; rightPadding: 5
        quiet: true
    }
    function focusSearch() { if (searchField) { searchField.forceActiveFocus(); searchField.selectAll(); } }
    function revealSelection() {
        list.forceLayout();
        if (backend.selectedIndex >= 0) list.positionViewAtIndex(backend.selectedIndex, ListView.Contain);
    }
    onVisibleChanged: if (visible) Qt.callLater(revealSelection)
    function contentTitle(entry) {
        if ((entry.kind === "text" || entry.kind === "rich") && entry.preview) {
            const first = entry.preview.split(/\r?\n/).find(line => line.trim().length > 0);
            if (first) return first.trim();
        }
        return entry.title || "";
    }
    function contentSnippet(entry) {
        if (!entry.preview) return "";
        const lines = entry.preview.split(/\r?\n/).map(line => line.trim()).filter(line => line.length > 0);
        if (entry.kind === "text" || entry.kind === "rich") return lines.slice(1).join("\n");
        if (entry.kind === "files") return lines.map(path => path.replace(/\\/g, "/").split("/").slice(0, -1).slice(-2).join("/")).filter((folder, index, all) => all.indexOf(folder) === index).join("\n");
        return "";
    }
    function relativeTime(milliseconds) {
        const minutes = Math.max(0, Math.floor((clock - Number(milliseconds)) / 60000));
        if (minutes < 1) return qsTranslate("Still", "刚刚");
        if (minutes < 60) return qsTranslate("Still", "%1 分钟前").arg(minutes);
        const hours = Math.floor(minutes / 60);
        if (hours < 24) return qsTranslate("Still", "%1 小时前").arg(hours);
        return qsTranslate("Still", "%1 天前").arg(Math.floor(hours / 24));
    }
    function handleKey(event, fromSearch) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            backend.activate((event.modifiers & Qt.ControlModifier) ? ((event.modifiers & Qt.ShiftModifier) ? "plain" : "copy") : "");
        } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            if (fromSearch) list.forceActiveFocus();
            backend.selectedIndex += event.key === Qt.Key_Down ? 1 : -1;
        } else if (!fromSearch && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            backend.selectedIndex += event.key === Qt.Key_Right ? 1 : -1;
        } else if (!fromSearch && event.matches(StandardKey.Copy)) {
            backend.activate("copy");
        } else if (!fromSearch && event.key === Qt.Key_Space && root.previewFits) {
            backend.previewVisible = !backend.previewVisible;
        } else if (!fromSearch && event.key === Qt.Key_Delete) {
            backend.removeSelected();
        } else { return; }
        event.accepted = true;
    }
    Timer { interval: 60000; repeat: true; running: root.visible; onTriggered: root.clock = Date.now() }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        RowLayout {
            objectName: "ClipboardFilter"
            visible: backend.filter !== "pinned"
            spacing: 4
            Repeater {
                model: [{ text: qsTranslate("Still", "全部"), key: "all" }, { text: qsTranslate("Still", "文字"), key: "text" }, { text: qsTranslate("Still", "图片"), key: "image" }, { text: qsTranslate("Still", "文件"), key: "files" }, { text: qsTranslate("Still", "其他"), key: "other" }]
                delegate: UiButton {
                    required property var modelData
                    objectName: "Filter_" + modelData.key
                    text: modelData.text
                    implicitHeight: 32
                    leftPadding: 12
                    rightPadding: leftPadding
                    quiet: true
                    checked: backend.filter === modelData.key
                    onClicked: backend.filter = modelData.key
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? Theme.dark ? Theme.navSelected : Theme.selection : parent.down ? Theme.selection : parent.hovered ? Theme.hover : "transparent"
                        border.color: parent.visualFocus ? Theme.focus : parent.checked ? Theme.border : "transparent"
                        Rectangle {
                            visible: Theme.dark && parent.parent.checked
                            anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top
                            width: 16; height: 3; radius: 1.5; color: Theme.focus
                        }
                    }
                }
            }
        }
        Label { visible: backend.filter === "pinned"; text: qsTranslate("Still", "已固定"); font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.ink }
        Item { Layout.fillWidth: true }
        UiButton {
            objectName: "ClipboardMore"; glyph: "more"; quiet: true
            ToolTip.text: qsTranslate("Still", "更多操作")
            onClicked: {
                const point = mapToItem(root, width, height + 6);
                contextMenu.x = point.x - contextMenu.width; contextMenu.y = point.y;
                contextMenu.open();
            }
        }
        UiButton {
            objectName: "ClipboardPaste"
            visible: backend.pasteDefault
            quiet: true
            implicitHeight: 32
            keyHint: root.previewFits ? "Enter" : ""
            text: qsTranslate("Still", "粘贴到原窗口")
            ToolTip.text: text + " (Enter)"
            enabled: backend.selectedIndex >= 0
            onClicked: backend.activate()
            background: Rectangle {
                radius: 8
                color: parent.down ? Theme.selection : parent.hovered ? Theme.hover : "transparent"
                border.color: parent.visualFocus ? Theme.focus : "transparent"
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 16
        ListView {
            id: list
            objectName: "ClipboardList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 244
            onHeightChanged: Qt.callLater(root.revealSelection)
            spacing: 4
            topMargin: 2
            bottomMargin: 2
            footer: Item { height: root.captureClearance }
            model: backend.clips
            currentIndex: backend.selectedIndex
            clip: true
            keyNavigationEnabled: false
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            Accessible.name: qsTranslate("Still", "剪贴板历史")
            Keys.onPressed: event => root.handleKey(event, false)
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: card
                required property int index
                required property var modelData
                readonly property bool selected: backend.selectedIndex === index && !root.searchField.activeFocus
                readonly property bool actionsRevealed: selected || rowHover.hovered || copyAction.visualFocus || pinAction.visualFocus || deleteAction.visualFocus
                objectName: "ClipRow" + index
                x: 2
                width: list.width - 4
                height: 86
                radius: 4
                color: selected ? Theme.selection : rowHover.hovered ? Theme.hover : "transparent"
                border.color: selected && list.activeFocus ? Theme.focus : "transparent"
                border.width: 1
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.title + "，" + modelData.detail
                Accessible.onPressAction: { backend.selectedIndex = index; backend.activate(); }
                HoverHandler { id: rowHover }
                Rectangle {
                    visible: Theme.dark && card.selected && !list.activeFocus
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2; height: 20; radius: 1; color: Theme.focus
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: event => {
                        backend.selectedIndex = card.index; list.forceActiveFocus();
                        if (event.button === Qt.RightButton) {
                            const point = mapToItem(root, event.x, event.y);
                            contextMenu.x = point.x; contextMenu.y = point.y; contextMenu.open();
                        }
                    }
                    onDoubleClicked: event => { if (event.button === Qt.LeftButton) backend.activate(); }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12; anchors.rightMargin: 12
                    spacing: 14
                    Item {
                        Layout.preferredWidth: card.modelData.kind === "image" ? 48 : 28
                        Layout.preferredHeight: 52
                        Image {
                            anchors.centerIn: parent
                            width: card.modelData.kind === "image" ? parent.width - 4 : 20
                            height: card.modelData.kind === "image" ? 48 : 20
                            source: card.modelData.kind === "image" ? card.modelData.thumbnail || "" : "image://content/icon/" + card.modelData.kind + (Theme.dark ? "-dark" : "")
                            sourceSize: Qt.size(96, 96)
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 16
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text {
                                objectName: "ClipTitle" + card.index
                                text: root.contentTitle(card.modelData)
                                textFormat: Text.PlainText
                                font.family: Theme.fontFamily; font.pixelSize: 15; font.weight: Font.DemiBold
                                color: Theme.ink
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                objectName: "ClipTime" + card.index
                                text: root.relativeTime(card.modelData.createdMs)
                                font.family: Theme.fontFamily; font.pixelSize: 11
                                color: card.selected ? Theme.selectedMuted : Theme.muted
                                Accessible.description: card.modelData.createdText
                            }
                        }
                        Text {
                            objectName: "ClipSnippet" + card.index
                            text: root.contentSnippet(card.modelData) || card.modelData.kindLabel
                            textFormat: Text.PlainText
                            font.family: Theme.fontFamily; font.pixelSize: 12
                            color: card.selected ? Theme.selectedMuted : Theme.muted
                            elide: Text.ElideRight
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            rightPadding: card.actionsRevealed ? 104 : card.modelData.pinned ? 76 : 0
                            Layout.fillWidth: true
                            Accessible.description: card.modelData.kind === "files" ? card.modelData.preview : ""
                        }
                    }
                }
                Rectangle {
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                    width: 104; height: 32
                    radius: 6
                    color: card.selected ? Theme.selection : card.actionsRevealed ? Theme.hover : "transparent"
                    visible: card.actionsRevealed || card.modelData.pinned
                    Item {
                        anchors.centerIn: parent
                        width: 92; height: 28
                        RowAction {
                            id: copyAction
                            objectName: "Copy" + card.index
                            anchors.right: pinAction.left; anchors.rightMargin: 4
                            glyph: "copy"
                            visible: card.actionsRevealed
                            ToolTip.text: qsTranslate("Still", "复制内容")
                            onClicked: { backend.selectedIndex = card.index; backend.activate("copy"); }
                        }
                        RowAction {
                            id: pinAction
                            objectName: "Pin" + card.index
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: "star"; checked: !!card.modelData.pinned
                            visible: card.actionsRevealed || card.modelData.pinned
                            ToolTip.text: checked ? qsTranslate("Still", "取消固定") : qsTranslate("Still", "固定内容")
                            onClicked: { backend.selectedIndex = card.index; backend.togglePin(); }
                        }
                        RowAction {
                            id: deleteAction
                            objectName: "Delete" + card.index
                            anchors.right: parent.right
                            glyph: "trash"
                            visible: card.actionsRevealed
                            ToolTip.text: qsTranslate("Still", "删除这条记录")
                            onClicked: {
                                backend.selectedIndex = card.index;
                                list.forceActiveFocus();
                                backend.removeSelected();
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 320)
                visible: backend.clips.length === 0
                spacing: 14
                Image { source: "image://content/icon/clipboard" + (Theme.dark ? "-dark" : ""); sourceSize: Qt.size(112, 112); Layout.preferredWidth: 56; Layout.preferredHeight: 56; Layout.alignment: Qt.AlignHCenter }
                Label {
                    objectName: "ClipboardEmpty"
                    text: backend.totalCount === 0 ? qsTranslate("Still", "还没有内容") : qsTranslate("Still", "没有匹配的内容")
                    color: Theme.ink; font.pixelSize: 18; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true
                }
                Label {
                    text: backend.totalCount > 0 ? qsTranslate("Still", "试试其他关键词，或切换内容类型。") : !backend.form.clipboard.enabled ? qsTranslate("Still", "剪贴板记录已关闭，可在偏好设置中开启。") : backend.paused ? qsTranslate("Still", "已暂停记录，继续后会收集新的复制内容。") : qsTranslate("Still", "复制文字、图片或文件后，内容会出现在这里。")
                    color: Theme.muted; font.pixelSize: 14; lineHeight: 1.5; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
            }
        }
        Rectangle {
            visible: backend.previewVisible && root.previewFits
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.border
        }
        Item {
            objectName: "ClipboardPreview"
            visible: backend.previewVisible && root.previewFits
            Layout.preferredWidth: Math.min(320, root.width * 0.4)
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 4; anchors.topMargin: 6; anchors.bottomMargin: 6 + root.captureClearance; spacing: 16
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: backend.selected.kindLabel || qsTranslate("Still", "选择一条内容"); color: Theme.muted; font.pixelSize: 12; Layout.fillWidth: true }
                    UiButton { visible: backend.selected.kind === "image"; glyph: "edit"; quiet: true; implicitWidth: 28; leftPadding: 5; rightPadding: 5; ToolTip.text: qsTranslate("Still", "编辑图片"); onClicked: backend.activate("edit") }
                    UiButton { visible: root.canPin; glyph: "pin"; quiet: true; implicitWidth: 28; leftPadding: 5; rightPadding: 5; ToolTip.text: root.pinLabel; onClicked: backend.activate("pin") }
                }
                Image {
                    objectName: "ClipboardImagePreview"
                    visible: backend.selected.kind === "image"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.preferredWidth: 0; Layout.preferredHeight: 0
                    source: backend.previewImage || backend.selected.thumbnail || ""
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    // Use the stable viewport, not image geometry changed by decoding.
                    sourceSize: Qt.size(Math.max(1, Math.round(parent.width * Screen.devicePixelRatio)), Math.max(1, Math.round(parent.height * Screen.devicePixelRatio)))
                }
                ScrollView {
                    visible: backend.selected.kind !== "image"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    TextArea { objectName: "ClipboardTextPreview"; text: backend.previewText; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: Theme.fontFamily; font.pixelSize: 15; color: Theme.ink; selectionColor: Theme.accent; selectedTextColor: "white"; padding: 0; background: null; Accessible.name: qsTranslate("Still", "内容预览") }
                }
                Label { text: backend.selected.createdText || ""; font.pixelSize: 11; color: Theme.muted; Accessible.description: backend.selected.formats || ""; ToolTip.text: backend.selected.formats || ""; ToolTip.visible: formatHover.hovered && ToolTip.text.length > 0; ToolTip.delay: 600; HoverHandler { id: formatHover } }
            }
        }
    }
    Label { visible: root.previewFits; text: qsTranslate("Still", "空格预览"); color: Theme.muted; font.pixelSize: 12 }
    Menu {
        id: contextMenu
        parent: root
        objectName: "ClipboardContextMenu"
        popupType: Popup.Item
        focus: true
        width: 232
        padding: 6
        margins: 8
        background: Rectangle { objectName: "ClipboardMenuSurface"; radius: 9; color: Theme.surface; border.color: Theme.border }
        delegate: MenuItem {
            id: menuEntry
            objectName: action ? action.objectName : ""
            implicitHeight: 36
            leftPadding: 12; rightPadding: 12
            font.family: Theme.fontFamily; font.pixelSize: 13
            contentItem: Text {
                text: menuEntry.text
                textFormat: Text.PlainText
                font: menuEntry.font
                color: menuEntry.enabled ? Theme.ink : Theme.muted
                opacity: menuEntry.enabled ? 1 : 0.45
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: 6
                color: menuEntry.highlighted ? Theme.selection : menuEntry.hovered ? Theme.hover : "transparent"
                border.color: menuEntry.visualFocus ? Theme.focus : "transparent"
            }
        }
        Action { objectName: "ClipboardMenuCopy"; text: qsTranslate("Still", "复制内容"); enabled: backend.selectedIndex >= 0; onTriggered: backend.activate("copy") }
        Action {
            objectName: "ClipboardMenuPreview"
            text: backend.previewVisible ? qsTranslate("Still", "隐藏预览") : qsTranslate("Still", "显示预览")
            enabled: root.previewFits
            onTriggered: backend.previewVisible = !backend.previewVisible
        }
        Action { objectName: "ClipboardMenuPin"; text: backend.selected.pinned ? qsTranslate("Still", "取消固定") : qsTranslate("Still", "固定内容"); enabled: backend.selectedIndex >= 0; onTriggered: backend.togglePin() }
        Action { text: qsTranslate("Still", "纯文本粘贴"); enabled: backend.selectedIndex >= 0 && backend.selected.kind !== "image" && backend.selected.kind !== "data"; onTriggered: backend.activate("plain") }
        MenuSeparator { topPadding: 4; bottomPadding: 4; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        Action { text: qsTranslate("Still", "编辑图片"); enabled: backend.selected.kind === "image"; onTriggered: backend.activate("edit") }
        Action { text: root.pinLabel; enabled: root.canPin; onTriggered: backend.activate("pin") }
        Action { text: qsTranslate("Still", "保存图片"); enabled: backend.selected.kind === "image"; onTriggered: backend.activate("save") }
        MenuSeparator { topPadding: 4; bottomPadding: 4; contentItem: Rectangle { implicitHeight: 1; color: Theme.border } }
        Action { text: qsTranslate("Still", "删除选中"); enabled: backend.selectedIndex >= 0; onTriggered: backend.removeSelected() }
        Action { text: qsTranslate("Still", "清空历史…"); enabled: backend.totalCount > 0; onTriggered: backend.clearClipboard() }
    }
    Connections { target: backend; function onSelectionChanged() { Qt.callLater(root.revealSelection); } }
}
