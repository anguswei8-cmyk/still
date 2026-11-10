pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: backend ? backend.dark : false
    readonly property color surface: dark ? "#1c1c1c" : "#ffffff"
    readonly property color sidebar: dark ? "#191919" : "#e9ebef"
    readonly property color navInk: ink
    readonly property color navMuted: dark ? "#adadad" : "#626875"
    readonly property color navSelected: dark ? "#383838" : "#ffffff"
    readonly property color canvas: dark ? "#181818" : "#f4f5f7"
    readonly property color ink: dark ? "#ededed" : "#242731"
    readonly property color muted: dark ? "#adadad" : "#626875"
    readonly property color accent: dark ? "#555555" : "#454b57"
    readonly property color focus: dark ? "#a8a8a8" : accent
    readonly property color primary: dark ? "#343434" : accent
    readonly property color primaryInk: "#ffffff"
    readonly property color primaryHover: dark ? "#404040" : "#353b46"
    readonly property color primaryPressed: dark ? "#242424" : "#272d37"
    readonly property color selection: dark ? "#292929" : "#e8ebf0"
    readonly property color selectedMuted: dark ? "#c4c4c4" : "#525d70"
    readonly property color border: dark ? "#383838" : "#d9dde5"
    readonly property color hover: dark ? "#242424" : "#e2e6ed"
    readonly property color glass: dark ? "#242424" : "#bdf7f8fb"
    readonly property color glassRim: dark ? "#18ffffff" : "#66ffffff"
    readonly property color navigationGlass: dark ? "#cc191919" : "#c4f8fafc"
    readonly property color error: dark ? "#ff9b90" : "#b42318"
    readonly property int buttonRadius: 9
    readonly property int searchRadius: 12
    readonly property int sectionTopSpacing: 24
    readonly property int sectionBottomSpacing: 16
    readonly property int historyGap: 20
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : "Helvetica Neue"
}
