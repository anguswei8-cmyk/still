#pragma once

#include <QAbstractTextDocumentLayout>
#include <QTextDocument>

namespace still {

class SafeHtmlDocument final : public QTextDocument, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit SafeHtmlDocument(const QString &html, QObject *parent = nullptr);
    QSizeF intrinsicSize(QTextDocument *, int, const QTextFormat &) override { return QSizeF(0, 0); }
    void drawObject(QPainter *, const QRectF &, QTextDocument *, int, const QTextFormat &) override {}
protected:
    QVariant loadResource(int type, const QUrl &name) override;
};

} // namespace still
