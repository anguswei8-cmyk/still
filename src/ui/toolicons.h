#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace pixclone {

enum class ToolIcon {
    Edit, Copy, Save, Pin, Ocr, Scroll, Close, Apply,
    Rectangle, RoundedRectangle, Ellipse, Line, Arrow, Pen, Highlight,
    Mosaic, MosaicPen, Text, Number, Color, Undo, Redo, More,
    Clipboard, Files, Image, Link, Star, Trash, Pause, Settings, Keyboard, Capture,
    TextSnippet, Ellipsis, Search, Preview, ChevronDown, Minimize, Maximize, Restore, Back
};

QIcon toolIcon(ToolIcon icon, QColor color = QColor("#e7edf6"));
QString toolbarStyleSheet();

} // namespace pixclone
