#include "html_document.h"
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>

namespace still {

SafeHtmlDocument::SafeHtmlDocument(const QString &html, QObject *parent) : QTextDocument(parent) {
    // Qt's default image handler can read files without loadResource(), even after a failed resource lookup.
    documentLayout()->registerHandler(QTextFormat::ImageObject, this);
    setHtml(html);
    QVector<QPair<int, int>> images;
    for (auto block = begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.charFormat().isImageFormat()) images.append({fragment.position(), fragment.length()});
        }
    }
    QTextCursor cursor(this);
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        cursor.setPosition(it->first); cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor); cursor.removeSelectedText();
    }
    // HTML titles otherwise escape into QToolTip's separate rich-text document.
    QTextCharFormat inactive; inactive.setToolTip({}); inactive.setAnchor(false); inactive.setAnchorHref({});
    cursor.select(QTextCursor::Document); cursor.mergeCharFormat(inactive); cursor.mergeBlockCharFormat(inactive);
}

QVariant SafeHtmlDocument::loadResource(int, const QUrl &) {
    // A valid empty value also blocks instance/global providers; an invalid QVariant would fall through.
    return QByteArray();
}

} // namespace still
