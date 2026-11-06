#include "ui/i18n.h"
#include "image.h"

#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QSet>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace still {

namespace {

QRectF shapeRect(const Annotation &a) {
    return QRectF(a.points.first(), a.points.last()).normalized();
}

QPainterPath pointPath(const Annotation &a) {
    QPainterPath path(a.points.first());
    for (int i = 1; i < a.points.size(); ++i) path.lineTo(a.points[i]);
    return path;
}

QPainterPath stroke(const QPainterPath &path, qreal width) {
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path);
}

QPainterPath mosaicMask(const Annotation &a) {
    QPainterPath mask;
    if (a.tool == Tool::Mosaic) mask.addRect(shapeRect(a));
    else {
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(12.0, a.width * 5));
        stroker.setCapStyle(Qt::RoundCap);
        mask = stroker.createStroke(pointPath(a));
        if (a.points.size() == 1) mask.addEllipse(a.points.first(), a.width * 2.5, a.width * 2.5);
    }
    return mask;
}

QPolygonF arrowHead(const Annotation &a) {
    const QPointF end = a.points.last();
    const QPointF delta = end - a.points.first();
    const qreal angle = std::atan2(delta.y(), delta.x());
    const qreal length = qMax(12.0, a.width * 4);
    return {end, end - QPointF(std::cos(angle - 0.5) * length, std::sin(angle - 0.5) * length),
                 end - QPointF(std::cos(angle + 0.5) * length, std::sin(angle + 0.5) * length)};
}

QFont textFont(const Annotation &a) {
    QFont font;
    font.setPixelSize(qMax(16, qRound(a.width * 6)));
    return font;
}

QRectF textRect(const Annotation &a, const QSize &imageSize) {
    const QPointF start = a.points.first();
    return QRectF(start, QSizeF(imageSize.width() - start.x(), imageSize.height() - start.y()));
}

constexpr int textFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;

QPainterPath annotationShape(const Annotation &a, const QSize &imageSize) {
    QPainterPath path;
    switch (a.tool) {
    case Tool::Rectangle: path.addRect(shapeRect(a)); break;
    case Tool::RoundedRectangle: path.addRoundedRect(shapeRect(a), 12, 12); break;
    case Tool::Ellipse: path.addEllipse(shapeRect(a)); break;
    case Tool::Line:
    case Tool::Arrow:
        path.moveTo(a.points.first());
        path.lineTo(a.points.last());
        break;
    case Tool::Pen:
    case Tool::Highlight: path = pointPath(a); break;
    case Tool::Mosaic:
    case Tool::MosaicPen: return mosaicMask(a);
    case Tool::Text:
        path.addRect(QFontMetricsF(textFont(a)).boundingRect(textRect(a, imageSize), textFlags, a.text));
        return path;
    case Tool::Number: {
        const qreal radius = qMax(12.0, a.width * 4) + a.width / 2;
        path.addEllipse(a.points.first(), radius, radius);
        return path;
    }
    }
    const qreal width = a.tool == Tool::Highlight ? qMax(14.0, a.width * 5) : a.width;
    QPainterPath result = stroke(path, width);
    if ((a.tool == Tool::Pen && a.points.size() == 1)
        || ((a.tool == Tool::Line || a.tool == Tool::Arrow) && a.points.first() == a.points.last()))
        result.addEllipse(a.points.first(), width / 2, width / 2);
    if (a.tool == Tool::Arrow) {
        QPainterPath head;
        head.addPolygon(arrowHead(a));
        head.closeSubpath();
        result = result.united(head).united(stroke(head, width));
    }
    return result;
}

void applyColorEffect(QImage &image, const QRect &area, ImageEffect effect, int amount) {
    const int offset = qRound(amount * 255.0 / 100);
    for (int y = area.top(); y < area.y() + area.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = area.left(); x < area.x() + area.width(); ++x) {
            const QRgb pixel = pixels[x];
            switch (effect) {
            case ImageEffect::Grayscale: {
                const int gray = qGray(pixel);
                pixels[x] = qRgba(gray, gray, gray, qAlpha(pixel));
                break;
            }
            case ImageEffect::Invert:
                pixels[x] = qRgba(255 - qRed(pixel), 255 - qGreen(pixel), 255 - qBlue(pixel), qAlpha(pixel));
                break;
            case ImageEffect::Brightness:
                pixels[x] = qRgba(qBound(0, qRed(pixel) + offset, 255), qBound(0, qGreen(pixel) + offset, 255),
                                  qBound(0, qBlue(pixel) + offset, 255), qAlpha(pixel));
                break;
            }
        }
    }
}

} // namespace

ImageDocument::ImageDocument(QImage image) : original_(image.convertToFormat(QImage::Format_ARGB32)) {
    original_.setDevicePixelRatio(1);
    currentFrame_.sourceRect = original_.rect();
}

void ImageDocument::add(const Annotation &annotation) {
    if (annotation.points.isEmpty()) return;
    edit(AnnotationChange{int(annotations_.size()), std::nullopt, annotation, {currentFrame_, nextOrder_++}});
}

void ImageDocument::replace(int index, const Annotation &annotation) {
    if (annotations_[index] == annotation) return;
    edit(AnnotationChange{index, annotations_[index], annotation, annotationContexts_[index]});
}

void ImageDocument::remove(int index) {
    edit(AnnotationChange{index, annotations_[index], std::nullopt, annotationContexts_[index]});
}

bool ImageDocument::crop(const QRect &selection) {
    const QRect area = selection.normalized().intersected(QRect(QPoint(), size()));
    if (area.isEmpty() || area.size() == size()) return false;
    Frame frame = currentFrame_;
    const QPoint offset(frame.horizontal ? size().width() - area.x() - area.width() : area.x(),
                        frame.vertical ? size().height() - area.y() - area.height() : area.y());
    frame.sourceRect = QRect(frame.sourceRect.topLeft() + offset, area.size());
    edit(FrameChange{currentFrame_, frame});
    return true;
}

void ImageDocument::flip(Qt::Orientation orientation) {
    Frame frame = currentFrame_;
    if (orientation == Qt::Horizontal) frame.horizontal = !frame.horizontal;
    else frame.vertical = !frame.vertical;
    edit(FrameChange{currentFrame_, frame});
}

void ImageDocument::applyEffect(ImageEffect effect, int amount) {
    if (effect == ImageEffect::Brightness && amount == 0) return;
    edit(EffectChange{effect, currentFrame_.sourceRect, nextOrder_++, amount});
}

void ImageDocument::edit(Change change) {
    changes_.remove(position_, changes_.size() - position_);
    changes_.append(change);
    apply(change, true);
    ++position_;
    ++revision_;
}

void ImageDocument::apply(const Change &change, bool forward) {
    if (const auto *frame = std::get_if<FrameChange>(&change)) {
        currentFrame_ = forward ? frame->after : frame->before;
        return;
    }
    if (const auto *effect = std::get_if<EffectChange>(&change)) {
        if (forward) effects_.append(*effect);
        else effects_.removeLast();
        return;
    }
    const auto &annotation = std::get<AnnotationChange>(change);
    const auto &before = forward ? annotation.before : annotation.after;
    const auto &after = forward ? annotation.after : annotation.before;
    if (before && after) annotations_[annotation.index] = *after;
    else if (after) {
        annotations_.insert(annotation.index, *after);
        annotationContexts_.insert(annotation.index, annotation.context);
    } else {
        annotations_.removeAt(annotation.index);
        annotationContexts_.removeAt(annotation.index);
    }
}

void ImageDocument::undo() { if (canUndo()) { apply(changes_[--position_], false); ++revision_; } }
void ImageDocument::redo() { if (canRedo()) { apply(changes_[position_++], true); ++revision_; } }

QImage ImageDocument::render() const {
    return render(-1, {});
}

QImage ImageDocument::previewBrightness(int amount) const {
    QImage image = render();
    applyColorEffect(image, image.rect(), ImageEffect::Brightness, amount);
    return image;
}

QImage ImageDocument::project(const QImage &image, const Frame &frame) {
    QImage local = frame.sourceRect == image.rect() ? image : image.copy(frame.sourceRect);
    return frame.horizontal || frame.vertical ? local.mirrored(frame.horizontal, frame.vertical) : local;
}

void ImageDocument::paste(QImage &image, const Frame &frame, const QImage &local) {
    for (int y = 0; y < local.height(); ++y) {
        auto *target = reinterpret_cast<QRgb *>(image.scanLine(frame.sourceRect.y() + y)) + frame.sourceRect.x();
        const auto *source = reinterpret_cast<const QRgb *>(local.constScanLine(frame.vertical ? local.height() - 1 - y : y));
        if (frame.horizontal) std::reverse_copy(source, source + local.width(), target);
        else std::memcpy(target, source, size_t(local.width()) * sizeof(QRgb));
    }
}

QImage ImageDocument::render(int index, const Annotation &replacement) const {
    QImage result = original_;
    for (int first = 0, effectIndex = 0; first < annotations_.size() || effectIndex < effects_.size();) {
        if (effectIndex < effects_.size()
            && (first == annotations_.size() || effects_[effectIndex].order < annotationContexts_[first].order)) {
            const auto &effect = effects_[effectIndex++];
            applyColorEffect(result, effect.area, effect.effect, effect.amount);
            continue;
        }
        int last = first + 1;
        const auto &frame = annotationContexts_[first].frame;
        while (last < annotations_.size() && annotationContexts_[last].frame == frame
               && (effectIndex == effects_.size() || annotationContexts_[last].order < effects_[effectIndex].order)) ++last;
        // Keep each authoring canvas intact: text wrapping and mosaic sampling depend on its extent.
        if (frame.sourceRect == result.rect() && !frame.horizontal && !frame.vertical) {
            for (int i = first; i < last; ++i) paintAnnotation(result, i == index ? replacement : annotations_[i]);
        } else {
            QImage local = project(result, frame);
            for (int i = first; i < last; ++i) paintAnnotation(local, i == index ? replacement : annotations_[i]);
            paste(result, frame, local);
        }
        first = last;
    }
    return project(result, currentFrame_);
}

QTransform ImageDocument::transform(const Frame &frame) const {
    const QPointF origin(frame.sourceRect.x() + (frame.horizontal ? frame.sourceRect.width() : 0),
                         frame.sourceRect.y() + (frame.vertical ? frame.sourceRect.height() : 0));
    const qreal x = currentFrame_.horizontal ? currentFrame_.sourceRect.x() + size().width() - origin.x()
                                           : origin.x() - currentFrame_.sourceRect.x();
    const qreal y = currentFrame_.vertical ? currentFrame_.sourceRect.y() + size().height() - origin.y()
                                         : origin.y() - currentFrame_.sourceRect.y();
    return QTransform(frame.horizontal == currentFrame_.horizontal ? 1 : -1, 0, 0,
                      frame.vertical == currentFrame_.vertical ? 1 : -1, x, y);
}

QPainterPath ImageDocument::shape(int index, const Annotation &annotation) const {
    const auto &frame = annotationContexts_[index].frame;
    QPainterPath authoringClip;
    authoringClip.addRect(QRectF(QPointF(), frame.sourceRect.size()));
    QPainterPath imageClip;
    imageClip.addRect(QRectF(QPointF(), size()));
    return transform(frame).map(annotationShape(annotation, frame.sourceRect.size()).intersected(authoringClip)).intersected(imageClip);
}

QRectF ImageDocument::bounds(int index) const {
    return bounds(index, annotations_[index]);
}

QRectF ImageDocument::bounds(int index, const Annotation &replacement) const {
    return shape(index, replacement).boundingRect();
}

Annotation ImageDocument::translated(int index, QPointF currentDelta) const {
    const QTransform mapping = transform(annotationContexts_[index].frame);
    auto result = annotations_[index];
    const QPointF first = mapping.map(result.points.first());
    qreal left = first.x(), right = left, top = first.y(), bottom = top;
    for (const auto &point : result.points) {
        const QPointF current = mapping.map(point);
        left = qMin(left, current.x()); right = qMax(right, current.x());
        top = qMin(top, current.y()); bottom = qMax(bottom, current.y());
    }
    const auto clamp = [](qreal delta, qreal start, qreal end, qreal extent) {
        return qBound(qMin(0.0, qMin(-start, extent - end)), delta, qMax(0.0, qMax(-start, extent - end)));
    };
    currentDelta.setX(clamp(currentDelta.x(), left, right, size().width()));
    currentDelta.setY(clamp(currentDelta.y(), top, bottom, size().height()));
    const QPointF localDelta(currentDelta.x() * mapping.m11(), currentDelta.y() * mapping.m22());
    for (auto &point : result.points) point += localDelta;
    return result;
}

int ImageDocument::hitTest(const QPointF &point, qreal tolerance) const {
    if (!QRectF(QPointF(), size()).contains(point)) return -1;
    for (int i = int(annotations_.size()) - 1; i >= 0; --i) {
        const QPainterPath visible = shape(i, annotations_[i]);
        if (visible.contains(point) || (tolerance > 0 && stroke(visible, tolerance * 2).contains(point))) return i;
    }
    return -1;
}

int ImageDocument::nextNumber() const {
    QSet<int> used;
    for (const auto &annotation : annotations_) if (annotation.tool == Tool::Number) used.insert(annotation.text.toInt());
    int number = 1;
    while (used.contains(number)) ++number;
    return number;
}

QRectF annotationBounds(const Annotation &annotation, const QSize &imageSize) {
    return annotationShape(annotation, imageSize).boundingRect();
}

void paintAnnotation(QImage &image, const Annotation &a) {
    if (image.isNull() || a.points.isEmpty()) return;
    const QPointF start = a.points.first();
    const QPointF end = a.points.last();
    const QRectF bounds = shapeRect(a);
    const QPainterPath path = pointPath(a);
    if (a.tool == Tool::Mosaic || a.tool == Tool::MosaicPen) {
        const QPainterPath mask = mosaicMask(a);
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
        painter.setBrush(a.color);
        painter.drawPolygon(arrowHead(a));
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
        painter.setFont(textFont(a));
        painter.drawText(textRect(a, image.size()), textFlags, a.text);
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

QRectF logicalRect(const QRect &pixels, const QSizeF &logicalSize, const QSize &pixelSize) {
    const qreal sx = pixelSize.width() / logicalSize.width();
    const qreal sy = pixelSize.height() / logicalSize.height();
    qreal left = pixels.x() / sx, top = pixels.y() / sy;
    const int rightPixel = pixels.x() + pixels.width(), bottomPixel = pixels.y() + pixels.height();
    qreal right = rightPixel == pixelSize.width() ? logicalSize.width() : rightPixel / sx;
    qreal bottom = bottomPixel == pixelSize.height() ? logicalSize.height() : bottomPixel / sy;
    QRectF result(QPointF(left, top), QPointF(right, bottom));
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    // Correct representable edges after QRectF reconstruction, without changing outward crop rounding.
    for (QRect actual = pixelRect(result, logicalSize, pixelSize); actual != pixels; actual = pixelRect(result, logicalSize, pixelSize)) {
        if (actual.left() < pixels.left()) left = std::nextafter(left, infinity);
        if (actual.top() < pixels.top()) top = std::nextafter(top, infinity);
        if (actual.right() > pixels.right()) right = std::nextafter(right, -infinity);
        if (actual.bottom() > pixels.bottom()) bottom = std::nextafter(bottom, -infinity);
        result = QRectF(QPointF(left, top), QPointF(right, bottom));
    }
    return result;
}

QImage cropFrame(const QImage &image, const QSizeF &logicalSize, const QRectF &selection) {
    const QRect crop = pixelRect(selection, logicalSize, image.size());
    if (crop.isEmpty()) return {};
    QImage result = image.copy(crop);
    // All downstream image/OCR coordinates are physical pixels, including QPainter composition.
    result.setDevicePixelRatio(1);
    return result;
}

int wordAt(const QVector<OcrWord> &words, const QPointF &point, bool snapToNearest) {
    for (int i = 0; i < words.size(); ++i) if (words[i].bounds.contains(point)) return i;
    if (!snapToNearest) return -1;
    int nearest = -1;
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 0; i < words.size(); ++i) {
        const auto &bounds = words[i].bounds;
        const QPointF delta = point - QPointF(qBound(bounds.left(), point.x(), bounds.right()),
                                              qBound(bounds.top(), point.y(), bounds.bottom()));
        const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
        if (distance < best) { best = distance; nearest = i; }
    }
    return nearest;
}

QString selectedText(const OcrResult &result, int anchor, int cursor) {
    if (anchor < 0 || cursor < 0 || anchor >= result.words.size() || cursor >= result.words.size()) return {};
    const auto &first = result.words[qMin(anchor, cursor)];
    const auto &last = result.words[qMax(anchor, cursor)];
    return result.text.mid(first.textStart, last.textStart + last.textLength - first.textStart);
}

OcrResult parseOcr(const QByteArray &json, const QSize &imageSize) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {{}, {}, t("OCR 返回了无效 JSON")};
    const auto object = document.object();
    if (!object.value("ok").toBool()) return {{}, {}, object.value("message").toString(t("OCR 识别失败"))};
    if (!object.value("text").isString() || !object.value("words").isArray()) return {{}, {}, t("OCR 返回了无效 JSON")};
    OcrResult result;
    result.text = object.value("text").toString();
    const QRectF imageBounds(QPointF(), imageSize);
    for (const auto value : object.value("words").toArray()) {
        const auto word = value.toObject();
        const qreal x = word.value("x").toDouble();
        const qreal y = word.value("y").toDouble();
        const qreal w = word.value("width").toDouble();
        const qreal h = word.value("height").toDouble();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) continue;
        const QRectF bounds = QRectF(x, y, w, h).intersected(imageBounds);
        const qint64 start = word.value("start").toInteger(-1), length = word.value("length").toInteger(-1);
        if (start < 0 || start > result.text.size() || length <= 0 || length > result.text.size() - start) continue;
        if (!result.words.isEmpty() && start < result.words.last().textStart + result.words.last().textLength) continue;
        if (!bounds.isEmpty()) result.words.append({bounds, qsizetype(start), qsizetype(length)});
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
