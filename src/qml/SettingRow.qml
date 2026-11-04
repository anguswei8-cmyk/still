import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var field
    property bool first: false
    readonly property bool section: field.type === "section"
    spacing: 0
    Rectangle {
        visible: root.section && !root.first
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.bottomMargin: 18
        height: 1
        color: Theme.border
    }
    Label {
        visible: root.section
        text: root.field.title
        font.pixelSize: 13
        font.weight: Font.DemiBold
        color: Theme.ink
        Layout.bottomMargin: 10
    }
    RowLayout {
        visible: !root.section
        Layout.fillWidth: true
        Layout.minimumHeight: root.field.group === "appearance" && root.field.key === "theme" ? 116 : root.field.description ? 68 : 50
        spacing: 22
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: root.field.title
                textFormat: Text.PlainText
                font.pixelSize: 14
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: !!root.field.description
                text: root.field.description || ""
                textFormat: Text.PlainText
                font.pixelSize: 12
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.bottomMargin: 4
            }
        }
        Loader {
            active: !root.section
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: root.field.group === "appearance" ? (root.field.key === "theme" ? 330 : 240) : root.field.type === "switch" ? 40 : root.field.type === "number" ? 150 : root.field.type === "directory" ? 280 : root.field.type === "folder" ? 100 : 180
            sourceComponent: root.field.group === "appearance" ? (root.field.key === "theme" ? appearanceControl : languageControl) : root.field.type === "switch" ? toggleControl : root.field.type === "choice" ? choiceControl : root.field.type === "number" ? numberControl : root.field.type === "directory" ? directoryControl : root.field.type === "shortcut" ? shortcutControl : root.field.type === "folder" ? folderControl : textControl
        }
    }
    Component {
        id: appearanceControl
        AppearancePicker { objectName: "appearance_theme" }
    }
    Component {
        id: languageControl
        LanguagePicker { objectName: "appearance_language" }
    }
    Component {
        id: toggleControl
        UiSwitch {
            objectName: root.field.group + "_" + root.field.key
            checked: !!backend.form[root.field.group][root.field.key]
            Accessible.name: root.field.title
            Accessible.description: root.field.description || ""
            onToggled: backend.setSetting(root.field.group, root.field.key, checked)
        }
    }
    Component {
        id: textControl
        UiField {
            objectName: root.field.group + "_" + root.field.key
            text: backend.form[root.field.group][root.field.key]
            Accessible.name: root.field.title
            Accessible.description: root.field.description || ""
            onTextEdited: backend.setDraft(root.field.group, root.field.key, text)
            onEditingFinished: backend.setSetting(root.field.group, root.field.key, text)
        }
    }
    Component {
        id: directoryControl
        RowLayout {
            spacing: 6
            UiField {
                objectName: root.field.group + "_dir"
                Layout.fillWidth: true
                Layout.minimumWidth: 110
                text: backend.form[root.field.group].dir
                placeholderText: root.field.placeholder || ""
                Accessible.name: root.field.title
                Accessible.description: root.field.description || ""
                onTextEdited: backend.setDraft(root.field.group, "dir", text)
                onEditingFinished: backend.setSetting(root.field.group, "dir", text)
            }
            UiButton { text: qsTranslate("Still", "选择"); onClicked: backend.browseDirectory(root.field.group) }
        }
    }
    Component {
        id: choiceControl
        UiComboBox {
            id: choice
            objectName: root.field.group + "_" + root.field.key
            implicitHeight: 36
            font.family: Theme.fontFamily
            font.pixelSize: 13
            model: root.field.options
            textRole: "text"
            valueRole: "value"
            currentIndex: {
                const value = backend.form[root.field.group][root.field.key];
                for (let i = 0; i < root.field.options.length; ++i) if (root.field.options[i].value === value) return i;
                return -1;
            }
            Accessible.name: root.field.title
            Accessible.description: root.field.description || ""
            onActivated: backend.setSetting(root.field.group, root.field.key, currentValue)
        }
    }
    Component {
        id: numberControl
        RowLayout {
            spacing: 7
            UiSpinBox {
                id: number
                objectName: root.field.group + "_" + root.field.key
                Layout.fillWidth: true
                implicitHeight: 36
                editable: true
                from: root.field.minimum
                to: root.field.maximum
                value: Number(backend.form[root.field.group][root.field.key])
                font.family: Theme.fontFamily
                font.pixelSize: 13
                Accessible.name: root.field.title
                Accessible.description: root.field.description || ""
                onValueModified: backend.setSetting(root.field.group, root.field.key, value)
            }
            Label { visible: !!root.field.unit; text: root.field.unit || ""; font.pixelSize: 12; color: Theme.muted }
        }
    }
    Component {
        id: shortcutControl
        UiField {
            objectName: "hotkey_" + root.field.key
            text: { const form = backend.form; return backend.shortcutText(root.field.key); }
            placeholderText: activeFocus ? qsTranslate("Still", "按下新的组合键") : qsTranslate("Still", "点击录入")
            readOnly: true
            Accessible.name: root.field.title
            Accessible.description: qsTranslate("Still", "点击录入组合键，退格键清空")
            onActiveFocusChanged: if (activeFocus) selectAll()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) { event.accepted = false; return; }
                backend.setShortcut(root.field.key, event.key, event.modifiers);
                event.accepted = true;
            }
        }
    }
    Component {
        id: folderControl
        UiButton { text: qsTranslate("Still", "打开目录"); Accessible.name: root.field.title; onClicked: backend.openDirectory(root.field.group) }
    }
}
