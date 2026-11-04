import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

Pane {
    id: root
    objectName: "QmlShell"
    readonly property real captureClearance: captureButton.height + 12
    component WindowButton: UiButton {
        implicitWidth: 34
        implicitHeight: 34
        leftPadding: (width - contentItem.implicitWidth) / 2
        rightPadding: leftPadding
        quiet: true
        activeFocusOnTab: false
    }
    padding: 0
    font.family: Theme.fontFamily
    font.pixelSize: 14
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accent
    palette.highlightedText: "white"
    palette.window: Theme.surface
    palette.windowText: Theme.ink
    palette.base: Theme.surface
    palette.button: Theme.surface
    palette.mid: Theme.border
    palette.dark: Theme.border
    readonly property bool preferences: backend.page !== 0 && backend.page !== 3
    property int lastLibraryPage: 0
    readonly property var pages: [
        { title: qsTranslate("Still", "剪贴板"), icon: "clipboard" },
        { title: qsTranslate("Still", "通用偏好"), icon: "general" },
        { title: qsTranslate("Still", "快捷键"), icon: "hotkeys" },
        { title: qsTranslate("Still", "截图历史"), icon: "history" },
        { title: qsTranslate("Still", "识别与限制"), icon: "advanced" },
        { title: qsTranslate("Still", "剪贴板设置"), icon: "settings" }
    ]
    readonly property var navigation: preferences ? [
        { page: 1, text: pages[1].title, icon: pages[1].icon },
        { page: 2, text: pages[2].title, icon: pages[2].icon },
        { page: 4, text: pages[4].title, icon: pages[4].icon },
        { page: 5, text: pages[5].title, icon: pages[5].icon }
    ] : [
        { page: 0, text: pages[0].title, icon: "clipboard" },
        { page: -1, text: qsTranslate("Still", "已固定"), icon: "star" },
        { page: 3, text: pages[3].title, icon: "history" }
    ]
    function navigate(page) {
        backend.page = page < 0 ? 0 : page;
        if (page <= 0) backend.filter = page < 0 ? "pinned" : "all";
    }
    background: Rectangle { radius: backend.maximized ? 0 : 12; color: backend.glassAvailable ? (Theme.dark ? "#e6181818" : "#c7f4f5f7") : Theme.canvas }
    Rectangle { width: 176; height: root.height; color: backend.glassAvailable ? Theme.navigationGlass : Theme.sidebar }
    Rectangle { x: 176; y: 48; width: root.width - 176; height: root.height - 48; color: backend.glassAvailable ? (Theme.dark ? "#f21c1c1c" : "#f2ffffff") : Theme.surface }

    Item {
        id: chrome
        width: parent.width
        height: 48
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: backend.moveWindow()
            onDoubleClicked: backend.toggleMaximized()
        }
        RowLayout {
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.preferredWidth: 176
                Layout.maximumWidth: 176
                Layout.fillWidth: false
                Layout.leftMargin: 20
                spacing: 10
                Image { source: "qrc:/brand/still.png"; sourceSize: Qt.size(64, 64); Layout.preferredWidth: 26; Layout.preferredHeight: 26 }
                Text { text: qsTranslate("Still", "拾影"); font.family: Theme.fontFamily; font.pixelSize: 16; font.weight: Font.DemiBold; color: Theme.navInk; Layout.fillWidth: true }
            }
            Item { Layout.fillWidth: true }
            RowLayout {
                visible: Qt.platform.os === "windows"
                Layout.rightMargin: 8
                spacing: 0
                WindowButton { objectName: "WindowMinimize"; glyph: "minimize"; ToolTip.text: qsTranslate("Still", "最小化"); onClicked: backend.showMinimized() }
                WindowButton { objectName: "WindowMaximize"; glyph: backend.maximized ? "restore" : "maximize"; ToolTip.text: backend.maximized ? qsTranslate("Still", "还原窗口") : qsTranslate("Still", "最大化"); onClicked: backend.toggleMaximized() }
                WindowButton {
                    id: closeButton
                    objectName: "WindowClose"
                    glyph: "close"; inverse: hovered
                    ToolTip.text: qsTranslate("Still", "关闭窗口")
                    onClicked: backend.close()
                    background: Rectangle { radius: Theme.dark ? 8 : height / 2; color: closeButton.hovered ? "#ce342b" : "transparent" }
                }
            }
        }
    }
    RowLayout {
        anchors.top: chrome.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        spacing: 0
        ColumnLayout {
            Layout.preferredWidth: 176
            Layout.minimumWidth: 176
            Layout.maximumWidth: 176
            Layout.fillHeight: true
            Layout.topMargin: 18
            Layout.bottomMargin: 20
            spacing: 12
            UiButton {
                objectName: "BackToLibrary"
                visible: root.preferences
                Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                quiet: true; glyph: "back"
                text: qsTranslate("Still", "返回内容")
                onClicked: { backend.page = root.lastLibraryPage; nav.forceActiveFocus(); }
            }
            ListView {
                id: nav
                objectName: "MainNavigation"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 6
                model: root.navigation
                currentIndex: root.preferences ? [1, 2, 4, 5].indexOf(backend.page) : backend.page === 3 ? 2 : backend.filter === "pinned" ? 1 : 0
                keyNavigationEnabled: false
                Accessible.name: qsTranslate("Still", "页面导航")
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                        const next = Math.max(0, Math.min(root.navigation.length - 1, currentIndex + (event.key === Qt.Key_Down ? 1 : -1)));
                        root.navigate(root.navigation[next].page);
                        event.accepted = true;
                    }
                }
                delegate: UiButton {
                    id: entry
                    required property int index
                    required property var modelData
                    objectName: modelData.page < 0 ? "NavigationPinned" : "Navigation" + modelData.page
                    width: nav.width; height: 42
                    quiet: true
                    checked: nav.currentIndex === index
                    text: modelData.text
                    contentItem: RowLayout {
                        spacing: 10
                        Image {
                            source: "image://content/icon/" + entry.modelData.icon + (entry.checked ? "-active" : "") + (Theme.dark ? "-dark" : "")
                            sourceSize: Qt.size(36, 36)
                            Layout.preferredWidth: 18; Layout.preferredHeight: 18
                        }
                        Text {
                            text: entry.text; textFormat: Text.PlainText; elide: Text.ElideRight
                            font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: entry.checked ? Font.DemiBold : Font.Normal
                            color: entry.checked ? Theme.navInk : Theme.navMuted
                            Layout.fillWidth: true
                        }
                    }
                    background: Rectangle { radius: 6; color: entry.checked ? Theme.navSelected : entry.hovered ? Theme.hover : "transparent"; border.color: entry.visualFocus ? Theme.focus : "transparent" }
                    onClicked: { root.navigate(modelData.page); nav.forceActiveFocus(); }
                }
            }
            UiButton {
                objectName: "ClipboardPause"
                visible: backend.page === 0
                Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                quiet: true; glyph: "pause"; checked: backend.paused
                text: backend.paused ? qsTranslate("Still", "继续记录") : qsTranslate("Still", "暂停记录")
                onClicked: backend.togglePause()
            }
            UiButton {
                objectName: "PreferencesEntry"
                visible: !root.preferences
                Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                quiet: true; glyph: "settings"
                text: qsTranslate("Still", "偏好设置")
                onClicked: { backend.page = 1; nav.forceActiveFocus(); }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 24
            Layout.topMargin: 12
            Layout.minimumWidth: 0
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                spacing: 20
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        objectName: "PageTitle"
                        text: root.pages[backend.page].title
                        font.family: Theme.fontFamily; font.pixelSize: 23; font.weight: Font.DemiBold
                        color: Theme.ink
                    }
                    Label {
                        objectName: "LibraryCount"
                        visible: backend.page === 0
                        text: qsTranslate("Still", "%1 条内容").arg(backend.clips.length)
                        font.pixelSize: 12; color: Theme.muted
                    }
                }
                Item { Layout.fillWidth: true }
                UiField {
                    id: search
                    objectName: "ClipboardSearch"
                    visible: backend.page === 0
                    Layout.preferredWidth: Math.min(360, root.width * 0.32)
                    Layout.minimumWidth: 200
                    implicitHeight: 40
                    leftPadding: 38; rightPadding: clearSearch.visible ? 38 : 12
                    text: backend.search
                    placeholderText: qsTranslate("Still", "搜索剪贴板")
                    Accessible.name: qsTranslate("Still", "搜索剪贴板历史")
                    onTextEdited: backend.search = text
                    Keys.onPressed: event => clipboardPage.handleKey(event, true)
                    background: GlassSurface {
                        radius: Theme.dark ? 8 : 20
                        border.color: Theme.dark ? (search.activeFocus ? "#737373" : "transparent") : (search.activeFocus ? Theme.focus : Theme.border)
                    }
                    Image { x: 13; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; source: "image://content/icon/search" + (Theme.dark ? "-dark" : ""); sourceSize: Qt.size(32, 32) }
                    UiButton {
                        id: clearSearch
                        anchors.right: parent.right; anchors.rightMargin: 3; anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; visible: search.text.length > 0; quiet: true; glyph: "close"
                        ToolTip.text: qsTranslate("Still", "清除搜索")
                        onClicked: { backend.search = ""; search.forceActiveFocus(); }
                    }
                }
                Label {
                    objectName: "SettingsStatus"
                    visible: root.preferences && text.length > 0
                    text: backend.status; textFormat: Text.PlainText
                    font.pixelSize: 12; color: backend.statusError ? Theme.error : Theme.muted
                    horizontalAlignment: Text.AlignRight
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                    Layout.maximumWidth: 260; Layout.preferredWidth: Math.min(implicitWidth, 260)
                    Accessible.name: qsTranslate("Still", "设置保存状态")
                    ToolTip.text: backend.status; ToolTip.visible: statusHover.hovered && backend.status.length > 0
                    HoverHandler { id: statusHover }
                }
            }
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumWidth: 0
                currentIndex: backend.page === 0 ? 0 : backend.page === 3 ? 1 : 2
                ClipboardPage { id: clipboardPage; searchField: search; captureClearance: root.captureClearance }
                HistoryPanel { captureClearance: root.captureClearance }
                Loader { active: root.preferences; sourceComponent: SettingsPage {} }
            }
            Label {
                objectName: "ActionMessage"
                visible: text.length > 0
                Layout.fillWidth: true
                Layout.rightMargin: captureButton.visible ? captureButton.width + 12 : 0
                text: backend.message; textFormat: Text.PlainText; font.pixelSize: 12
                color: Theme.muted; wrapMode: Text.Wrap; Accessible.name: text
            }
        }
    }
    UiButton {
        id: captureButton
        objectName: "MainCapture"
        visible: !root.preferences
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 24
        implicitWidth: 44
        implicitHeight: 44
        padding: 0
        leftPadding: (width - contentItem.implicitWidth) / 2
        rightPadding: leftPadding
        z: 1
        glyph: "capture"
        primary: true
        background: Rectangle {
            radius: height / 2
            color: captureButton.down ? Theme.primaryPressed : captureButton.hovered ? Theme.primaryHover : Theme.dark ? Theme.border : Theme.primary
            border.color: captureButton.visualFocus ? Theme.focus : Theme.glassRim
        }
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "black"
            shadowOpacity: Theme.dark ? 0.55 : 0.22
            shadowVerticalOffset: 4
            blurMax: 20
            shadowBlur: 0.65
        }
        ToolTip.text: qsTranslate("Still", "截图并编辑") + " (" + backend.shortcutText("capture") + ")"
        onClicked: backend.captureRequested()
    }
    Rectangle { anchors.fill: parent; radius: backend.maximized ? 0 : 12; color: "transparent"; border.width: backend.maximized ? 0 : 1; border.color: Theme.glassRim; z: 99 }
    Repeater {
        model: [
            { x: 0, y: 6, w: 5, h: root.height - 12, edge: Qt.LeftEdge, cursor: Qt.SizeHorCursor },
            { x: root.width - 5, y: 6, w: 5, h: root.height - 12, edge: Qt.RightEdge, cursor: Qt.SizeHorCursor },
            { x: 6, y: 0, w: root.width - 12, h: 5, edge: Qt.TopEdge, cursor: Qt.SizeVerCursor },
            { x: 6, y: root.height - 5, w: root.width - 12, h: 5, edge: Qt.BottomEdge, cursor: Qt.SizeVerCursor },
            { x: 0, y: 0, w: 7, h: 7, edge: Qt.LeftEdge | Qt.TopEdge, cursor: Qt.SizeFDiagCursor },
            { x: root.width - 7, y: 0, w: 7, h: 7, edge: Qt.RightEdge | Qt.TopEdge, cursor: Qt.SizeBDiagCursor },
            { x: 0, y: root.height - 7, w: 7, h: 7, edge: Qt.LeftEdge | Qt.BottomEdge, cursor: Qt.SizeBDiagCursor },
            { x: root.width - 7, y: root.height - 7, w: 7, h: 7, edge: Qt.RightEdge | Qt.BottomEdge, cursor: Qt.SizeFDiagCursor }
        ]
        delegate: MouseArea { required property var modelData; x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h; z: 100; enabled: Qt.platform.os === "windows" && !backend.maximized; cursorShape: modelData.cursor; onPressed: backend.resizeWindow(modelData.edge) }
    }
    Shortcut { sequence: StandardKey.Find; enabled: backend.page === 0; onActivated: clipboardPage.focusSearch() }
    Shortcut { sequence: "Ctrl+Return"; enabled: backend.page === 0; onActivated: backend.activate("copy") }
    Shortcut { sequence: "Ctrl+Shift+Return"; enabled: backend.page === 0; onActivated: backend.activate("plain") }
    Connections {
        target: backend
        function onPageChanged() { if (backend.page === 0 || backend.page === 3) root.lastLibraryPage = backend.page; }
        function onFocusSearchRequested() { clipboardPage.focusSearch(); }
    }
}
