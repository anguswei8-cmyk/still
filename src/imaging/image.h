#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace still {

enum class Tool { Rectangle, RoundedRectangle, Ellipse, Line, Arrow, Pen, Highlight, Mosaic, MosaicPen, Text, Number };

struct Annotation {
    Tool tool = Tool::Rectangle;
    QVector<QPointF> points;
    QColor color = QColor("#ff4c5b");
    qreal width = 3;
    QString text;
};

struct OcrWord {
    QString text;
    QRectF bounds; // Coordinates always refer to the unscaled source image's pixels.
    int line = 0;
};

struct OcrResult {
    QVector<OcrWord> words;
    QString error;
};

class ImageDocument {
public:
    explicit ImageDocument(QImage image = {});
    void add(const Annotation &annotation);
    void undo();
    void redo();
    bool canUndo() const { return position_ > 0; }
    bool canRedo() const { return position_ < annotations_.size(); }
    const QImage &original() const { return original_; }
    QImage render() const;
    int nextNumber() const;

private:
    QImage original_;
    QVector<Annotation> annotations_;
    int position_ = 0;
};

void paintAnnotation(QImage &image, const Annotation &annotation);
QRect pixelRect(const QRectF &selection, const QSizeF &logicalSize, const QSize &pixelSize);
QImage cropFrame(const QImage &image, const QSizeF &logicalSize, const QRectF &selection);
int wordAt(const QVector<OcrWord> &words, const QPointF &point);
QString selectedText(const QVector<OcrWord> &words, int anchor, int cursor);
OcrResult parseOcr(const QByteArray &json, const QSize &imageSize);
int findOverlap(const QImage &accumulated, const QImage &next);
QImage appendScrollFrame(const QImage &accumulated, const QImage &next, int maximumHeight = 24000, qint64 maximumBytes = 256LL * 1024 * 1024, bool *unmatched = nullptr);

} // namespace still
