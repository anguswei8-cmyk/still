#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <optional>
#include <variant>

class QPainterPath;
class QTransform;

namespace still {

enum class Tool { Rectangle, RoundedRectangle, Ellipse, Line, Arrow, Pen, Highlight, Mosaic, MosaicPen, Text, Number };
enum class ImageEffect { Grayscale, Invert, Brightness };

struct Annotation {
    Tool tool = Tool::Rectangle;
    QVector<QPointF> points;
    QColor color = QColor("#ff4c5b");
    qreal width = 3;
    QString text;

    bool operator==(const Annotation &other) const {
        return tool == other.tool && points == other.points && color == other.color && width == other.width && text == other.text;
    }
};

struct OcrWord {
    QRectF bounds; // Coordinates always refer to the unscaled source image's pixels.
    qsizetype textStart = 0;
    qsizetype textLength = 0;
};

struct OcrResult {
    QString text; // Selection text; word ranges are ordered, non-overlapping UTF-16 ranges into it.
    QVector<OcrWord> words;
    QString error;
};

class ImageDocument {
public:
    explicit ImageDocument(QImage image = {});
    void add(const Annotation &annotation);
    void replace(int index, const Annotation &annotation);
    void remove(int index);
    bool crop(const QRect &selection);
    void flip(Qt::Orientation orientation);
    void applyEffect(ImageEffect effect, int amount = 0);
    void undo();
    void redo();
    bool canUndo() const { return position_ > 0; }
    bool canRedo() const { return position_ < changes_.size(); }
    const QVector<Annotation> &annotations() const { return annotations_; }
    const QImage &original() const { return original_; }
    QSize size() const { return currentFrame_.sourceRect.size(); }
    quint64 revision() const { return revision_; }
    QImage render() const;
    QImage render(int index, const Annotation &replacement) const;
    QImage previewBrightness(int amount) const;
    QRectF bounds(int index) const;
    QRectF bounds(int index, const Annotation &replacement) const;
    Annotation translated(int index, QPointF currentDelta) const;
    int hitTest(const QPointF &point, qreal tolerance) const;
    int nextNumber() const;

private:
    struct Frame {
        QRect sourceRect;
        bool horizontal = false;
        bool vertical = false;
        bool operator==(const Frame &other) const {
            return sourceRect == other.sourceRect && horizontal == other.horizontal && vertical == other.vertical;
        }
    };
    struct AnnotationContext {
        Frame frame;
        quint64 order;
    };
    struct AnnotationChange {
        int index;
        std::optional<Annotation> before;
        std::optional<Annotation> after;
        AnnotationContext context;
    };
    struct FrameChange {
        Frame before;
        Frame after;
    };
    struct EffectChange {
        ImageEffect effect;
        QRect area;
        quint64 order;
        int amount = 0;
    };
    using Change = std::variant<AnnotationChange, FrameChange, EffectChange>;
    void edit(Change change);
    void apply(const Change &change, bool forward);
    static QImage project(const QImage &image, const Frame &frame);
    static void paste(QImage &image, const Frame &frame, const QImage &local);
    QTransform transform(const Frame &frame) const;
    QPainterPath shape(int index, const Annotation &annotation) const;
    QImage original_;
    Frame currentFrame_;
    QVector<Annotation> annotations_;
    QVector<AnnotationContext> annotationContexts_;
    QVector<EffectChange> effects_;
    QVector<Change> changes_;
    int position_ = 0;
    quint64 revision_ = 0;
    quint64 nextOrder_ = 0;
};

void paintAnnotation(QImage &image, const Annotation &annotation);
QRectF annotationBounds(const Annotation &annotation, const QSize &imageSize);
QRect pixelRect(const QRectF &selection, const QSizeF &logicalSize, const QSize &pixelSize);
// pixels must be nonempty and within pixelSize; both sizes must be positive.
QRectF logicalRect(const QRect &pixels, const QSizeF &logicalSize, const QSize &pixelSize);
QImage cropFrame(const QImage &image, const QSizeF &logicalSize, const QRectF &selection);
int wordAt(const QVector<OcrWord> &words, const QPointF &point, bool snapToNearest = false);
QString selectedText(const OcrResult &result, int anchor, int cursor);
OcrResult parseOcr(const QByteArray &json, const QSize &imageSize);
int findOverlap(const QImage &accumulated, const QImage &next);
QImage appendScrollFrame(const QImage &accumulated, const QImage &next, int maximumHeight = 24000, qint64 maximumBytes = 256LL * 1024 * 1024, bool *unmatched = nullptr);

} // namespace still
