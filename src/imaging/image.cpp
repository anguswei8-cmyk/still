#include "ui/i18n.h"
#include "image.h"

#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace still {

ImageDocument::ImageDocument(QImage image) : original_(image.convertToFormat(QImage::Format_ARGB32)) {
    original_.setDevicePixelRatio(1);
}

void ImageDocument::add(const Annotation &annotation) {
    if (annotation.points.isEmpty()) return;
    annotations_.resize(position_);
    annotations_.append(annotation);
    ++position_;
}

void ImageDocument::undo() { if (canUndo()) --position_; }
void ImageDocument::redo() { if (canRedo()) ++position_; }

QImage ImageDocument::render() const {
    QImage result = original_;
    for (int i = 0; i < position_; ++i) paintAnnotation(result, annotations_[i]);
    return result;
}

int ImageDocument::nextNumber() const {
    int count = 1;
    for (int i = 0; i < position_; ++i) if (annotations_[i].tool == Tool::Number) ++count;
    return count;
}

void paintAnnotation(QImage &image, const Annotation &a) {
    if (image.isNull() || a.points.isEmpty()) return;
    const QPointF start = a.points.first();
    const QPointF end = a.points.last();
    const QRectF bounds = QRectF(start, end).normalized();
    QPainterPath path(start);
    for (int i = 1; i < a.points.size(); ++i) path.lineTo(a.points[i]);
    if (a.tool == Tool::Mosaic || a.tool == Tool::MosaicPen) {
        QPainterPath mask;
        if (a.tool == Tool::Mosaic) mask.addRect(bounds);
        else {
            QPainterPathStroker stroker;
            stroker.setWidth(qMax(12.0, a.width * 5));
            stroker.setCapStyle(Qt::RoundCap);
            mask = stroker.createStroke(path);
            if (a.points.size() == 1) mask.addEllipse(start, a.width * 2.5, a.width * 2.5);
        }
        const QRect area = mask.boundingRect().toAlignedRect().intersected(image.rect());
        if (area.isEmpty()) return;
        const QImage block = image.copy(area).scaled(qMax(1, area.width() / 12), qMax(1, area.height() / 12),
                                                    Qt::IgnoreAspectRatio, Qt::FastTransformation)
                                     .scaled(area.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        QPainter painter(&image);
        painter.setClipPath(mask);
        painter.drawImage(area.topLeft(), block);
        return;
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (a.tool) {
    case Tool::Rectangle: painter.drawRect(bounds); break;
    case Tool::RoundedRectangle: painter.drawRoundedRect(bounds, 12, 12); break;
    case Tool::Ellipse: painter.drawEllipse(bounds); break;
    case Tool::Line: painter.drawLine(start, end); break;
    case Tool::Arrow: {
        painter.drawLine(start, end);
        const qreal angle = std::atan2(end.y() - start.y(), end.x() - start.x());
        const qreal length = qMax(12.0, a.width * 4);
        QPolygonF head;
        head << end << end - QPointF(std::cos(angle - 0.5) * length, std::sin(angle - 0.5) * length)
                    << end - QPointF(std::cos(angle + 0.5) * length, std::sin(angle + 0.5) * length);
        painter.setBrush(a.color);
        painter.drawPolygon(head);
        break;
    }
    case Tool::Highlight: {
        QColor color = a.color;
        color.setAlpha(85);
        painter.setPen(QPen(color, qMax(14.0, a.width * 5), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        break;
    }
    case Tool::Pen:
        if (a.points.size() == 1) painter.drawPoint(start);
        else painter.drawPath(path);
        break;
    case Tool::Text: {
        QFont font;
        font.setPixelSize(qMax(16, qRound(a.width * 6)));
        painter.setFont(font);
        painter.drawText(QRectF(start, QSizeF(image.width() - start.x(), image.height() - start.y())),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, a.text);
        break;
    }
    case Tool::Number: {
        const qreal radius = qMax(12.0, a.width * 4);
        painter.setBrush(a.color);
        painter.drawEllipse(start, radius, radius);
        painter.setPen(Qt::white);
        QFont font;
        font.setBold(true);
        font.setPixelSize(qRound(radius * 1.3));
        painter.setFont(font);
        painter.drawText(QRectF(start.x() - radius, start.y() - radius, radius * 2, radius * 2), Qt::AlignCenter, a.text);
        break;
    }
    default: break;
    }
}

QRect pixelRect(const QRectF &selection, const QSizeF &logicalSize, const QSize &pixelSize) {
    if (logicalSize.isEmpty() || pixelSize.isEmpty()) return {};
    const QRectF clipped = selection.normalized().intersected(QRectF(QPointF(), logicalSize));
    if (clipped.isEmpty()) return {};
    const qreal sx = pixelSize.width() / logicalSize.width();
    const qreal sy = pixelSize.height() / logicalSize.height();
    // Round outwards: a fractional-DPI selection must not lose its final row or column.
    const int left = qFloor(clipped.left() * sx);
    const int top = qFloor(clipped.top() * sy);
    return QRect(left, top, qCeil(clipped.right() * sx) - left, qCeil(clipped.bottom() * sy) - top)
        .intersected(QRect(QPoint(), pixelSize));
}

QImage cropFrame(const QImage &image, const QSizeF &logicalSize, const QRectF &selection) {
    const QRect crop = pixelRect(selection, logicalSize, image.size());
    if (crop.isEmpty()) return {};
    QImage result = image.copy(crop);
    // All downstream image/OCR coordinates are physical pixels, including QPainter composition.
    result.setDevicePixelRatio(1);
    return result;
}

int wordAt(const QVector<OcrWord> &words, const QPointF &point) {
    for (int i = 0; i < words.size(); ++i) if (words[i].bounds.contains(point)) return i;
    return -1;
}

QString selectedText(const QVector<OcrWord> &words, int anchor, int cursor) {
    if (anchor < 0 || cursor < 0 || anchor >= words.size() || cursor >= words.size()) return {};
    QString text;
    for (int i = qMin(anchor, cursor); i <= qMax(anchor, cursor); ++i) {
        if (!text.isEmpty()) {
            if (words[i - 1].line != words[i].line) text += '\n';
            else if (!text.back().isSpace() && !words[i].text.isEmpty()
                     && text.back().unicode() < 0x2e80 && words[i].text.front().unicode() < 0x2e80) text += ' ';
        }
        text += words[i].text;
    }
    return text;
}

OcrResult parseOcr(const QByteArray &json, const QSize &imageSize) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {{}, t("OCR 返回了无效 JSON")};
    const auto object = document.object();
    if (!object.value("ok").toBool()) return {{}, object.value("message").toString(t("OCR 识别失败"))};
    OcrResult result;
    const QRectF imageBounds(QPointF(), imageSize);
    for (const auto value : object.value("words").toArray()) {
        const auto word = value.toObject();
        const qreal x = word.value("x").toDouble();
        const qreal y = word.value("y").toDouble();
        const qreal w = word.value("width").toDouble();
        const qreal h = word.value("height").toDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) continue;
        const QRectF bounds = QRectF(x, y, w, h).intersected(imageBounds);
        const QString text = word.value("text").toString();
        if (!bounds.isEmpty() && !text.isEmpty()) result.words.append({text, bounds, word.value("line").toInt()});
    }
    return result;
}

int findOverlap(const QImage &accumulated, const QImage &next) {
    if (accumulated.width() != next.width() || accumulated.isNull() || next.isNull()) return -1;
    // Capture frames already use these layouts; avoid copying the entire accumulated long image.
    const auto rgb = [](const QImage &image) {
        return image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_RGB32);
    };
    const QImage a = rgb(accumulated);
    const QImage b = rgb(next);
    if (a.isNull() || b.isNull()) return -1;
    const int maximum = qMin(a.height(), b.height());
    const int step = qMax(1, a.width() / 64);
    bool identical = true;
    for (int y = 0; y < maximum && identical; ++y) {
        const auto *arow = reinterpret_cast<const QRgb *>(a.constScanLine(a.height() - maximum + y));
        const auto *brow = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) if ((arow[x] & 0xffffff) != (brow[x] & 0xffffff)) { identical = false; break; }
    }
    if (identical) return maximum;
    // Reject ambiguous static bands instead of silently deleting moving page content.
    for (int overlap = maximum - 1; overlap >= 12; --overlap) {
        qint64 error = 0;
        int samples = 0;
        int varyingRows = 0;
        for (int y = 0; y < overlap; ++y) {
            const auto *arow = reinterpret_cast<const QRgb *>(a.constScanLine(a.height() - overlap + y));
            const auto *brow = reinterpret_cast<const QRgb *>(b.constScanLine(y));
            const auto *previous = y ? reinterpret_cast<const QRgb *>(b.constScanLine(y - 1)) : brow;
            int variation = 0;
            for (int x = 0; x < a.width(); x += step) {
                error += qAbs(qRed(arow[x]) - qRed(brow[x])) + qAbs(qGreen(arow[x]) - qGreen(brow[x])) + qAbs(qBlue(arow[x]) - qBlue(brow[x]));
                variation += qAbs(qGray(brow[x]) - qGray(previous[x]));
                samples += 3;
            }
            if (variation > 30) ++varyingRows;
            if (error > qint64(samples) * 5) break;
        }
        if (samples && error <= qint64(samples) * 5 && varyingRows >= qMin(8, overlap / 2)) return overlap;
    }
    return -1;
}

QImage appendScrollFrame(const QImage &accumulated, const QImage &next, int maximumHeight, qint64 maximumBytes, bool *unmatched) {
    if (unmatched) *unmatched = false;
    if (next.isNull()) return {};
    const auto fits = [=](int width, qint64 height) { return width > 0 && height > 0 && height <= maximumHeight && height <= maximumBytes / 4 / width; };
    if (accumulated.isNull()) return fits(next.width(), next.height()) ? next : QImage();
    if (!fits(accumulated.width(), accumulated.height())) return {};
    const int overlap = findOverlap(accumulated, next);
    if (overlap < 0) { if (unmatched) *unmatched = true; return {}; }
    if (overlap >= next.height()) return accumulated;
    const qint64 height = qint64(accumulated.height()) + next.height() - overlap;
    if (!fits(accumulated.width(), height)) return {};
    QImage output(accumulated.width(), int(height), QImage::Format_ARGB32);
    if (output.isNull()) return {};
    QPainter painter(&output);
    painter.drawImage(0, 0, accumulated);
    painter.drawImage(0, accumulated.height(), next, 0, overlap, next.width(), next.height() - overlap);
    return output;
}

} // namespace still
