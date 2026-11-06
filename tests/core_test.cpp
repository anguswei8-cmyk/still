#include "imaging/image.h"
#include "storage/store.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>
#include <QtMath>

using namespace still;

static QImage page(int width = 60, int height = 360) {
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        image.setPixel(x, y, qRgb((y * 31 + x * 7) % 256, (y * 17 + x * 3) % 256, (y * 53 + x * 11) % 256));
    return image;
}

static QImage effectImage(QImage image, ImageEffect effect, int amount = 0) {
    if (effect == ImageEffect::Invert) image.invertPixels(QImage::InvertRgb);
    else for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        QColor color = image.pixelColor(x, y);
        if (effect == ImageEffect::Grayscale) {
            const int gray = qGray(color.rgb());
            color.setRgb(gray, gray, gray, color.alpha());
        } else {
            const int offset = qRound(amount * 255.0 / 100);
            color.setRgb(qBound(0, color.red() + offset, 255), qBound(0, color.green() + offset, 255),
                         qBound(0, color.blue() + offset, 255), color.alpha());
        }
        image.setPixelColor(x, y, color);
    }
    return image;
}

class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void cropUsesBitmapDimensions() {
        QCOMPARE(pixelRect(QRectF(10, 20, 30, 40), QSizeF(100, 100), QSize(200, 200)), QRect(20, 40, 60, 80));
        QCOMPARE(pixelRect(QRectF(10, 20, 30, 40), QSizeF(100, 100), QSize(50, 50)), QRect(5, 10, 15, 20));
    }
    void fractionalDpiRoundsOutwards() {
        QCOMPARE(pixelRect(QRectF(1, 1, 10, 10), QSizeF(100, 100), QSize(150, 150)), QRect(1, 1, 16, 16));
    }
    void cropClampsAndRejectsEmpty() {
        QCOMPARE(pixelRect(QRectF(-10, -10, 20, 20), QSizeF(100, 100), QSize(200, 200)), QRect(0, 0, 20, 20));
        QVERIFY(pixelRect(QRectF(200, 200, 10, 10), QSizeF(100, 100), QSize(200, 200)).isEmpty());
        QVERIFY(cropFrame(page(), QSizeF(), QRectF(0, 0, 10, 10)).isNull());
    }
    void logicalCropKeepsExactPixelEdges_data() {
        QTest::addColumn<QSizeF>("logicalSize"); QTest::addColumn<QSize>("pixelSize"); QTest::addColumn<QRect>("pixels");
        QTest::newRow("fractional-dpi") << QSizeF(160, 120) << QSize(240, 180) << QRect(15, 18, 32, 35);
        QTest::newRow("division-edge") << QSizeF(641, 400) << QSize(1536, 960) << QRect(176, 29, 1008, 187);
        QTest::newRow("rect-reconstruction-edge") << QSizeF(1093, 734) << QSize(641, 359) << QRect(129, 53, 359, 159);
        QTest::newRow("shrunk-bitmap-edge") << QSizeF(1093, 734) << QSize(641, 359) << QRect(89, 23, 96, 77);
        QTest::newRow("one-pixel") << QSizeF(1093, 734) << QSize(641, 359) << QRect(640, 358, 1, 1);
        QTest::newRow("one-pixel-bitmap") << QSizeF(1200, 900) << QSize(1, 1) << QRect(0, 0, 1, 1);
        QTest::newRow("full-frame") << QSizeF(1093, 734) << QSize(641, 359) << QRect(0, 0, 641, 359);
        QTest::newRow("right-bottom-edges") << QSizeF(1093, 734) << QSize(641, 359) << QRect(100, 50, 541, 309);
    }
    void logicalCropKeepsExactPixelEdges() {
        QFETCH(QSizeF, logicalSize); QFETCH(QSize, pixelSize); QFETCH(QRect, pixels);
        const QRectF selection = logicalRect(pixels, logicalSize, pixelSize);
        QVERIFY(QRectF(QPointF(), logicalSize).contains(selection));
        QCOMPARE(pixelRect(selection, logicalSize, pixelSize), pixels);
        const auto image = page(pixelSize.width(), pixelSize.height());
        const auto cropped = cropFrame(image, logicalSize, selection);
        QCOMPARE(cropped.size(), pixels.size());
        QCOMPARE(cropped, image.copy(pixels));
    }
    void logicalCropRoundTripsAtImageEdges() {
        const QVector<QPair<QSizeF, QSize>> scales{{{160, 120}, {240, 180}}, {{1093, 734}, {641, 359}},
            {{3840, 2160}, {1920, 1080}}, {{1536, 864}, {320, 180}}, {{800, 600}, {1200, 900}}};
        for (const auto &scale : scales) {
            const auto logicalSize = scale.first; const auto pixelSize = scale.second;
            for (int x : {0, 1, 23, pixelSize.width() / 2, pixelSize.width() - 1})
                for (int y : {0, 1, 17, pixelSize.height() / 2, pixelSize.height() - 1})
                    for (int width : {1, qMin(7, pixelSize.width() - x), pixelSize.width() - x})
                        for (int height : {1, qMin(9, pixelSize.height() - y), pixelSize.height() - y}) {
                            const QRect pixels(x, y, width, height);
                            const auto selection = logicalRect(pixels, logicalSize, pixelSize);
                            QVERIFY(QRectF(QPointF(), logicalSize).contains(selection));
                            QCOMPARE(pixelRect(selection, logicalSize, pixelSize), pixels);
                        }
        }
    }
    void cropPreservesExactPixels() {
        const auto image = page();
        QCOMPARE(cropFrame(image, QSizeF(30, 180), QRectF(5, 10, 12, 15)), image.copy(10, 20, 24, 30));
    }
    void highDpiCropsComposeAtActualPixelSize() {
        QImage source = page();
        source.setDevicePixelRatio(2);
        const auto crop = cropFrame(source, QSizeF(30, 180), QRectF(0, 0, 30, 50));
        QCOMPARE(crop.devicePixelRatio(), 1.0);
        QCOMPARE(crop.size(), QSize(60, 100));
        QCOMPARE(appendScrollFrame(crop, crop), crop);
    }
    void annotationsUndoRedoWithoutChangingOriginal() {
        const QImage source = page();
        ImageDocument document(source);
        document.add({Tool::Rectangle, {{5, 5}, {30, 30}}, Qt::red, 3, {}});
        QVERIFY(document.render() != source);
        QCOMPARE(document.original(), source);
        document.undo();
        QCOMPARE(document.render(), source);
        QVERIFY(document.canRedo());
        document.redo();
        QVERIFY(document.render() != source);
    }
    void newAnnotationDiscardsRedo() {
        ImageDocument document(page());
        document.add({Tool::Number, {{10, 10}}, Qt::red, 3, "1"});
        QCOMPARE(document.nextNumber(), 2);
        document.undo();
        QCOMPARE(document.nextNumber(), 1);
        document.add({Tool::Line, {{0, 0}, {10, 10}}, Qt::red, 2, {}});
        QVERIFY(!document.canRedo());
    }
    void annotationEditsAndDeletesRestoreOrder() {
        const auto source = page(120, 100);
        ImageDocument document(source);
        const Annotation first{Tool::Rectangle, {{5, 5}, {30, 30}}, Qt::red, 3, {}};
        const Annotation middle{Tool::Line, {{20, 40}, {90, 40}}, Qt::blue, 2, {}};
        const Annotation last{Tool::Number, {{80, 70}}, Qt::green, 3, "1"};
        document.add(first);
        document.add(middle);
        document.add(last);
        Annotation moved = middle;
        moved.points = {{30, 50}, {100, 50}};
        moved.color = Qt::yellow;
        moved.width = 5;
        document.replace(1, moved);
        document.remove(0);
        QVERIFY(document.annotations() == QVector<Annotation>({moved, last}));
        document.undo();
        QVERIFY(document.annotations() == QVector<Annotation>({first, moved, last}));
        document.undo();
        QVERIFY(document.annotations() == QVector<Annotation>({first, middle, last}));
        document.redo();
        document.redo();
        QVERIFY(document.annotations() == QVector<Annotation>({moved, last}));
        document.undo();
        QImage expected = source;
        for (const auto &annotation : QVector<Annotation>{first, moved, last}) paintAnnotation(expected, annotation);
        QCOMPARE(document.render(), expected);
        QCOMPARE(document.original(), source);
    }
    void replaceAndDeleteDiscardRedoButNoOpDoesNot() {
        ImageDocument document(page());
        const Annotation first{Tool::Line, {{0, 10}, {40, 10}}, Qt::red, 2, {}};
        Annotation changed = first;
        changed.color = Qt::blue;
        document.add(first);
        document.replace(0, changed);
        document.undo();
        document.replace(0, first);
        QVERIFY(document.canRedo());
        document.redo();
        QVERIFY(document.annotations().first() == changed);
        document.undo();
        changed.width = 6;
        document.replace(0, changed);
        QVERIFY(!document.canRedo());
        document.undo();
        document.remove(0);
        QVERIFY(document.annotations().isEmpty());
        QVERIFY(!document.canRedo());
        document.undo();
        QVERIFY(document.annotations().first() == first);
        document.replace(0, first);
        document.undo();
        QVERIFY(document.annotations().isEmpty());
    }
    void deletingNumbersReusesOnlyAnUnusedLabel() {
        ImageDocument document(page());
        for (int number = 1; number <= 3; ++number)
            document.add({Tool::Number, {{20, 20}}, Qt::red, 3, QString::number(number)});
        document.remove(1);
        QCOMPARE(document.nextNumber(), 2);
        document.add({Tool::Number, {{20, 20}}, Qt::red, 3, QString::number(document.nextNumber())});
        QCOMPARE(document.nextNumber(), 4);
        document.undo();
        QCOMPARE(document.nextNumber(), 2);
        document.undo();
        QCOMPARE(document.nextNumber(), 4);
        Annotation edited = document.annotations()[1];
        edited.text = "7";
        document.replace(1, edited);
        document.add({Tool::Text, {{20, 20}}, Qt::red, 3, "2"});
        QCOMPARE(document.nextNumber(), 2);
    }
    void hollowShapesHitOnlyTheirStrokeAndTopmostWins() {
        for (Tool tool : {Tool::Rectangle, Tool::RoundedRectangle, Tool::Ellipse}) {
            ImageDocument document(page(120, 120));
            document.add({Tool::Number, {{50, 50}}, Qt::blue, 3, "1"});
            document.add({tool, {{10, 10}, {90, 90}}, Qt::red, 4, {}});
            QCOMPARE(document.hitTest({50, 50}, 0), 0);
            QCOMPARE(document.hitTest({30, 50}, 0), -1);
            QCOMPARE(document.hitTest({10, 50}, 0), 1);
            QCOMPARE(document.hitTest({6, 50}, 0), -1);
            QCOMPARE(document.hitTest({6, 50}, 3), 1);
            if (tool != Tool::Rectangle) QCOMPARE(document.hitTest({10, 10}, 0), -1);
            document.add({Tool::Line, {{0, 50}, {100, 50}}, Qt::green, 4, {}});
            QCOMPARE(document.hitTest({10, 50}, 0), 2);
        }
    }
    void linePenAndArrowHitTheirPaintedGeometry() {
        ImageDocument document(page(140, 140));
        document.add({Tool::Line, {{20, 20}, {80, 20}}, Qt::red, 4, {}});
        QCOMPARE(document.hitTest({19, 20}, 0), 0);
        QCOMPARE(document.hitTest({40, 23}, 0), -1);
        QCOMPARE(document.hitTest({40, 23}, 2), 0);
        QCOMPARE(annotationBounds(document.annotations()[0], QSize(140, 140)), QRectF(18, 18, 64, 4));
        document.add({Tool::Pen, {{20, 50}, {50, 50}, {50, 80}}, Qt::red, 6, {}});
        QCOMPARE(document.hitTest({52, 49}, 0), 1);
        QCOMPARE(document.hitTest({35, 65}, 0), -1);
        document.add({Tool::Pen, {{100, 100}}, Qt::red, 6, {}});
        QCOMPARE(document.hitTest({102, 100}, 0), 2);
        QCOMPARE(document.hitTest({104, 100}, 0), -1);
        document.add({Tool::Arrow, {{20, 110}, {100, 110}}, Qt::red, 4, {}});
        QCOMPARE(document.hitTest({87, 116}, 0), 3);
        QCOMPARE(document.hitTest({70, 116}, 0), -1);
        QVERIFY(annotationBounds(document.annotations()[3], QSize(140, 140)).contains(QPointF(87, 116)));
    }
    void filledAndBroadToolsHitTheirActualWidths() {
        ImageDocument document(page(200, 200));
        document.add({Tool::Highlight, {{20, 20}, {80, 20}}, Qt::yellow, 1, {}});
        QCOMPARE(document.hitTest({50, 26}, 0), 0);
        QCOMPARE(document.hitTest({50, 28}, 0), -1);
        document.add({Tool::Mosaic, {{20, 40}, {80, 70}}, Qt::red, 3, {}});
        QCOMPARE(document.hitTest({50, 55}, 0), 1);
        QCOMPARE(annotationBounds(document.annotations()[1], QSize(200, 200)), QRectF(20, 40, 60, 30));
        document.add({Tool::MosaicPen, {{20, 90}, {80, 90}}, Qt::red, 2, {}});
        QCOMPARE(document.hitTest({50, 95}, 0), 2);
        QCOMPARE(document.hitTest({50, 97}, 0), -1);
        document.add({Tool::MosaicPen, {{100, 100}}, Qt::red, 2, {}});
        QCOMPARE(document.hitTest({104, 100}, 0), 3);
        QCOMPARE(document.hitTest({106, 100}, 0), -1);
        document.add({Tool::Number, {{140, 140}}, Qt::red, 3, "1"});
        QCOMPARE(document.hitTest({140, 140}, 0), 4);
        QCOMPARE(document.hitTest({153, 140}, 0), 4);
        QCOMPARE(document.hitTest({154, 140}, 0), -1);
    }
    void textBoundsFollowWrappedPainting() {
        const Annotation text{Tool::Text, {{70, 10}}, Qt::red, 3, "wrap wrap wrap wrap"};
        const QSize size(140, 180);
        const QRectF bounds = annotationBounds(text, size);
        QVERIFY(bounds.height() > annotationBounds(text, QSize(400, 180)).height());
        QVERIFY(bounds.width() <= 70);
        QImage image(size, QImage::Format_ARGB32);
        image.fill(Qt::white);
        ImageDocument document(image);
        document.add(text);
        QCOMPARE(document.hitTest(bounds.center(), 0), 0);
        QCOMPARE(document.hitTest({135, 165}, 0), -1);
        const QImage painted = document.render();
        QVERIFY(painted != image);
        for (int y = 0; y < painted.height(); ++y) for (int x = 0; x < painted.width(); ++x)
            if (painted.pixel(x, y) != image.pixel(x, y)) QVERIFY(bounds.adjusted(-1, -1, 1, 1).contains(QPointF(x, y)));
    }
    void mosaicEditsKeepCompositionOrder() {
        const auto source = page(120, 100);
        const Annotation line{Tool::Line, {{10, 40}, {100, 40}}, Qt::red, 10, {}};
        const Annotation mosaic{Tool::Mosaic, {{20, 20}, {90, 70}}, Qt::red, 3, {}};
        ImageDocument document(source);
        document.add(line);
        document.add(mosaic);
        QImage expected = source;
        paintAnnotation(expected, line);
        paintAnnotation(expected, mosaic);
        QCOMPARE(document.render(), expected);
        QImage reversed = source;
        paintAnnotation(reversed, mosaic);
        paintAnnotation(reversed, line);
        QVERIFY(expected != reversed);
        Annotation moved = line;
        moved.points = {{10, 55}, {100, 55}};
        document.replace(0, moved);
        QImage movedExpected = source;
        paintAnnotation(movedExpected, moved);
        paintAnnotation(movedExpected, mosaic);
        QCOMPARE(document.render(), movedExpected);
        document.undo();
        QCOMPARE(document.render(), expected);
        document.remove(0);
        document.undo();
        QCOMPARE(document.render(), expected);
        QCOMPARE(document.original(), source);
    }
    void mosaicChangesOnlyItsRegion() {
        QImage source = page();
        QImage image = source;
        paintAnnotation(image, {Tool::Mosaic, {{12, 12}, {48, 48}}, Qt::red, 3, {}});
        QVERIFY(image.copy(12, 12, 36, 36) != source.copy(12, 12, 36, 36));
        QCOMPARE(image.copy(0, 0, 10, 100), source.copy(0, 0, 10, 100));
    }
    void allToolsRender() {
        for (int tool = 0; tool <= int(Tool::Number); ++tool) {
            QImage source = page(120, 100);
            QImage image = source;
            paintAnnotation(image, {Tool(tool), {{15, 15}, {90, 70}}, QColor("#ff0000"), 4, "测试 1"});
            QVERIFY2(image != source, qPrintable(QString::number(tool)));
        }
    }
    void imageTransformsPreserveEveryToolsExactPixels() {
        QImage source = page(241, 181);
        for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = source.pixel(x, y);
            source.setPixel(x, y, qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), (x * 17 + y * 31) % 256));
        }
        const QRect crop(31, 23, 143, 111);
        for (int tool = 0; tool <= int(Tool::Number); ++tool) {
            ImageDocument document(source);
            const Annotation annotation{Tool(tool), {{57, 42}, {216, 156}}, QColor(237, 47, 83, 190), 4,
                                        tool == int(Tool::Number) ? "27" : "wrap wrap wrap wrap wrap wrap wrap wrap 123"};
            document.add(annotation);
            const QImage before = document.render();
            QVERIFY2(before != source, qPrintable(QString::number(tool)));
            QVERIFY(document.crop(crop));
            QImage expected = before.copy(crop);
            QCOMPARE(document.render(), expected);
            QCOMPARE(document.size(), crop.size());
            QCOMPARE(document.annotations(), QVector<Annotation>{annotation});
            document.flip(Qt::Horizontal);
            expected = expected.mirrored(true, false);
            QCOMPARE(document.render(), expected);
            document.flip(Qt::Vertical);
            expected = expected.mirrored(false, true);
            QCOMPARE(document.render(), expected);
            const QRect secondCrop(7, 9, 91, 77);
            QVERIFY(document.crop(secondCrop));
            expected = expected.copy(secondCrop);
            QCOMPARE(document.render(), expected);
            document.flip(Qt::Horizontal);
            QCOMPARE(document.render(), expected.mirrored(true, false));
            QCOMPARE(document.original(), source);
        }
    }
    void croppedMosaicsKeepTheirSamplingFrameAndCompositionOrder() {
        const QImage source = page(181, 139);
        const Annotation line{Tool::Line, {{0, 61}, {180, 61}}, Qt::red, 15, {}};
        const Annotation mosaic{Tool::Mosaic, {{11, 13}, {162, 118}}, Qt::black, 3, {}};
        const Annotation brush{Tool::MosaicPen, {{9, 41}, {161, 98}}, Qt::black, 4, {}};
        ImageDocument document(source);
        document.add(line); document.add(mosaic); document.add(brush);
        const QRect crop(37, 19, 99, 93);
        const QImage before = document.render();
        QVERIFY(document.crop(crop));
        document.flip(Qt::Horizontal);
        QCOMPARE(document.render(), before.copy(crop).mirrored(true, false));
        Annotation moved = line;
        for (auto &point : moved.points) point += QPointF(0, 7);
        QImage expected = source;
        paintAnnotation(expected, moved); paintAnnotation(expected, mosaic); paintAnnotation(expected, brush);
        expected = expected.copy(crop).mirrored(true, false);
        QCOMPARE(document.render(0, moved), expected);
        QCOMPARE(document.render(), before.copy(crop).mirrored(true, false));
        document.replace(0, moved);
        QCOMPARE(document.render(), expected);
        document.remove(0);
        QImage removed = source;
        paintAnnotation(removed, mosaic); paintAnnotation(removed, brush);
        QCOMPARE(document.render(), removed.copy(crop).mirrored(true, false));
        document.undo(); QCOMPARE(document.render(), expected);
        document.undo(); QCOMPARE(document.render(), before.copy(crop).mirrored(true, false));
        document.redo(); QCOMPARE(document.render(), expected);
        QCOMPARE(document.original(), source);
    }
    void newAnnotationsUseTheCurrentCroppedAndFlippedFrame() {
        QImage source = page(241, 181);
        for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = source.pixel(x, y);
            source.setPixel(x, y, qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), (x * 17 + y * 31) % 256));
        }
        ImageDocument document(source);
        document.add({Tool::Text, {{24, 19}}, Qt::red, 3, "original text 123"});
        document.add({Tool::Number, {{142, 60}}, Qt::green, 4, "19"});
        QVERIFY(document.crop(QRect(17, 11, 201, 153)));
        document.flip(Qt::Horizontal); document.flip(Qt::Vertical);
        QImage expected = document.render();
        const QVector<Annotation> added{
            {Tool::Text, {{14, 13}}, Qt::blue, 3, "new text wraps naturally 123 456"},
            {Tool::Number, {{153, 101}}, Qt::yellow, 4, "27"},
            {Tool::Rectangle, {{31, 49}, {161, 139}}, Qt::red, 3, {}},
            {Tool::Mosaic, {{17, 7}, {112, 98}}, Qt::black, 3, {}},
            {Tool::MosaicPen, {{19, 121}, {171, 113}}, Qt::black, 3, {}}
        };
        for (const auto &annotation : added) {
            paintAnnotation(expected, annotation);
            document.add(annotation);
            QCOMPARE(document.render(), expected);
        }
        Annotation edited = document.annotations()[2];
        edited.text = "edited text keeps the current frame";
        QImage editedExpected = source;
        paintAnnotation(editedExpected, document.annotations()[0]);
        paintAnnotation(editedExpected, document.annotations()[1]);
        editedExpected = editedExpected.copy(17, 11, 201, 153).mirrored(true, true);
        for (int i = 0; i < added.size(); ++i) paintAnnotation(editedExpected, i == 0 ? edited : added[i]);
        document.replace(2, edited);
        QCOMPARE(document.render(), editedExpected);
        document.undo(); QCOMPARE(document.render(), expected);
        document.redo(); QCOMPARE(document.render(), editedExpected);
        document.flip(Qt::Horizontal);
        expected = editedExpected.mirrored(true, false);
        QCOMPARE(document.render(), expected);
        const QRect crop(13, 9, 151, 111);
        QVERIFY(document.crop(crop));
        QCOMPARE(document.render(), expected.copy(crop));
        QCOMPARE(document.original(), source);
    }
    void transformedBoundsAndMovementKeepCroppedObjectsEditable() {
        const QImage source = page(200, 140);
        ImageDocument document(source);
        const Annotation line{Tool::Line, {{10, 60}, {190, 60}}, Qt::red, 4, {}};
        const Annotation hidden{Tool::Number, {{180, 120}}, Qt::green, 3, "1"};
        document.add(line); document.add(hidden);
        const QRect crop(50, 20, 80, 80);
        QVERIFY(document.crop(crop));
        QCOMPARE(document.bounds(0), QRectF(0, 38, 80, 4));
        QVERIFY(document.bounds(1).isEmpty());
        QCOMPARE(document.hitTest({1, 40}, 0), 0);
        QCOMPARE(document.hitTest({40, 40}, 0), 0);
        QCOMPARE(document.hitTest({40, 50}, 0), -1);
        QCOMPARE(document.hitTest({-1, 40}, 5), -1);
        QVERIFY(document.translated(0, {}) == line);
        QCOMPARE(document.translated(0, {1, 0}).points, QVector<QPointF>({{11, 60}, {191, 60}}));
        QCOMPARE(document.translated(0, {999, 999}).points, QVector<QPointF>({{50, 100}, {230, 100}}));
        document.flip(Qt::Horizontal); document.flip(Qt::Vertical);
        QVERIFY(document.translated(0, {}) == line);
        const Annotation moved = document.translated(0, {1, 1});
        QCOMPARE(moved.points, QVector<QPointF>({{9, 59}, {189, 59}}));
        QImage expected = source;
        paintAnnotation(expected, moved); paintAnnotation(expected, hidden);
        expected = expected.copy(crop).mirrored(true, true);
        QCOMPARE(document.render(0, moved), expected);
        QCOMPARE(document.bounds(0, moved), QRectF(0, 39, 80, 4));
        document.replace(0, moved);
        QCOMPARE(document.render(), expected);
        QCOMPARE(document.hitTest({40, 41}, 0), 0);
        const Annotation fresh{Tool::Line, {{20, 20}, {40, 20}}, Qt::blue, 2, {}};
        document.add(fresh);
        QCOMPARE(document.translated(2, {1, 1}).points, QVector<QPointF>({{21, 21}, {41, 21}}));
        QCOMPARE(document.hitTest({30, 20}, 0), 2);
    }
    void transformedTextBoundsUseTheOriginalWrappingWidth() {
        ImageDocument document(page(220, 180));
        const Annotation text{Tool::Text, {{70, 10}}, Qt::red, 3, "wrap wrap wrap wrap wrap wrap wrap wrap"};
        document.add(text);
        const QRect crop(60, 0, 90, 130);
        QVERIFY(document.crop(crop));
        const QRectF expected = annotationBounds(text, QSize(220, 180)).translated(-60, 0)
                                    .intersected(QRectF(QPointF(), crop.size()));
        QCOMPARE(document.bounds(0), expected);
        QCOMPARE(document.hitTest(expected.center(), 0), 0);
        document.flip(Qt::Horizontal);
        const QRectF mirrored(crop.width() - expected.right(), expected.top(), expected.width(), expected.height());
        QCOMPARE(document.bounds(0), mirrored);
        QCOMPARE(document.hitTest(mirrored.center(), 0), 0);
    }
    void mixedImageAndObjectChangesShareOneUndoHistory() {
        const QImage source = page(221, 181);
        ImageDocument document(source);
        QVector<QImage> images{source};
        QVector<QVector<Annotation>> objects{{}};
        const auto record = [&] { images.append(document.render()); objects.append(document.annotations()); };
        document.add({Tool::Rectangle, {{12, 17}, {190, 155}}, Qt::red, 4, {}}); record();
        QVERIFY(document.crop(QRect(31, 23, 143, 111))); record();
        document.flip(Qt::Horizontal); record();
        document.add({Tool::Text, {{13, 11}}, Qt::blue, 3, "new text 123"}); record();
        document.replace(0, document.translated(0, {4, -3})); record();
        document.remove(1); record();
        document.flip(Qt::Vertical); record();
        for (int i = images.size() - 2; i >= 0; --i) {
            document.undo();
            QCOMPARE(document.render(), images[i]);
            QCOMPARE(document.size(), images[i].size());
            QCOMPARE(document.annotations(), objects[i]);
            QCOMPARE(document.original(), source);
        }
        QVERIFY(!document.canUndo());
        for (int i = 1; i < images.size(); ++i) {
            document.redo();
            QCOMPARE(document.render(), images[i]);
            QCOMPARE(document.annotations(), objects[i]);
        }
        QVERIFY(!document.canRedo());
        document.undo();
        QVERIFY(document.canRedo());
        QVERIFY(!document.crop(QRect(QPoint(), document.size())));
        QVERIFY(!document.crop(QRect(-100, -100, 1000, 1000)));
        QVERIFY(!document.crop(QRect()));
        QVERIFY(!document.crop(QRect(500, 500, 20, 20)));
        document.replace(0, document.annotations()[0]);
        QVERIFY(document.canRedo());
        document.redo(); QCOMPARE(document.render(), images.last());
        document.undo();
        const QImage beforeBranch = document.render();
        const QRect branchCrop(3, 5, 91, 77);
        QVERIFY(document.crop(branchCrop));
        QCOMPARE(document.render(), beforeBranch.copy(branchCrop));
        QVERIFY(!document.canRedo());
        document.undo(); QCOMPARE(document.render(), beforeBranch);
        document.flip(Qt::Vertical);
        QVERIFY(!document.canRedo());
        QCOMPARE(document.render(), beforeBranch.mirrored(false, true));
    }
    void documentCropNormalizesAndClipsPixelSelections() {
        const QImage source = page(101, 81);
        ImageDocument document(source);
        QVERIFY(document.crop(QRect(80, 60, -60, -45)));
        QCOMPARE(document.render(), source.copy(20, 15, 60, 45));
        document.undo();
        QVERIFY(document.crop(QRect(-5, -4, 20, 15)));
        QCOMPARE(document.render(), source.copy(0, 0, 15, 11));
        QVERIFY(!document.canRedo());
        ImageDocument empty;
        QVERIFY(!empty.crop(QRect(0, 0, 10, 10)));
        QVERIFY(!empty.canUndo());
    }
    void documentRevisionChangesOnlyWhenTheImageDocumentChanges() {
        ImageDocument document(page(101, 81));
        QCOMPARE(document.revision(), quint64(0));
        document.undo(); document.redo(); document.add({});
        QVERIFY(!document.crop(QRect(QPoint(), document.size())));
        QVERIFY(!document.crop(QRect(200, 200, 10, 10)));
        QCOMPARE(document.revision(), quint64(0));
        const Annotation line{Tool::Line, {{10, 20}, {80, 20}}, Qt::red, 3, {}};
        document.add(line); QCOMPARE(document.revision(), quint64(1));
        document.replace(0, line); QCOMPARE(document.revision(), quint64(1));
        document.flip(Qt::Horizontal); QCOMPARE(document.revision(), quint64(2));
        QVERIFY(document.crop(QRect(5, 7, 71, 53)));
        QCOMPARE(document.revision(), quint64(3));
        document.undo(); QCOMPARE(document.revision(), quint64(4));
        document.redo(); QCOMPARE(document.revision(), quint64(5));
        document.redo(); QCOMPARE(document.revision(), quint64(5));
        document.undo(); QCOMPARE(document.revision(), quint64(6));
        QVERIFY(document.crop(QRect(3, 5, 61, 47)));
        QCOMPARE(document.revision(), quint64(7));
        QVERIFY(!document.canRedo());
        document.redo(); document.replace(0, line);
        QCOMPARE(document.revision(), quint64(7));
        document.remove(0); QCOMPARE(document.revision(), quint64(8));
    }
    void imageEffectsChangeRgbAndPreserveAlphaAndOriginal() {
        QImage source(2, 2, QImage::Format_ARGB32);
        source.setPixel(0, 0, qRgba(160, 32, 96, 0));
        source.setPixel(1, 0, qRgba(12, 120, 244, 64));
        source.setPixel(0, 1, qRgba(255, 0, 0, 128));
        source.setPixel(1, 1, qRgba(0, 255, 0, 255));
        QImage gray(2, 2, QImage::Format_ARGB32);
        gray.setPixel(0, 0, qRgba(86, 86, 86, 0));
        gray.setPixel(1, 0, qRgba(102, 102, 102, 64));
        gray.setPixel(0, 1, qRgba(87, 87, 87, 128));
        gray.setPixel(1, 1, qRgba(127, 127, 127, 255));
        QImage inverted(2, 2, QImage::Format_ARGB32);
        inverted.setPixel(0, 0, qRgba(95, 223, 159, 0));
        inverted.setPixel(1, 0, qRgba(243, 135, 11, 64));
        inverted.setPixel(0, 1, qRgba(0, 255, 255, 128));
        inverted.setPixel(1, 1, qRgba(255, 0, 255, 255));
        for (ImageEffect effect : {ImageEffect::Grayscale, ImageEffect::Invert}) {
            ImageDocument document(source);
            const QImage expected = effect == ImageEffect::Grayscale ? gray : inverted;
            document.applyEffect(effect);
            QCOMPARE(document.render(), expected);
            QCOMPARE(document.original(), source);
            QVERIFY(document.annotations().isEmpty());
            QCOMPARE(document.revision(), quint64(1));
            document.undo(); QCOMPARE(document.render(), source);
            QCOMPARE(document.revision(), quint64(2));
            document.redo(); QCOMPARE(document.render(), expected);
            QCOMPARE(document.revision(), quint64(3));
        }
        ImageDocument unchanged(gray);
        unchanged.applyEffect(ImageEffect::Grayscale);
        unchanged.applyEffect(ImageEffect::Grayscale);
        QCOMPARE(unchanged.render(), gray);
        QCOMPARE(unchanged.revision(), quint64(2));
        unchanged.undo(); QVERIFY(unchanged.canUndo());
        unchanged.undo(); QVERIFY(!unchanged.canUndo());
        QCOMPARE(unchanged.revision(), quint64(4));
        ImageDocument empty;
        empty.applyEffect(ImageEffect::Invert);
        QVERIFY(empty.render().isNull());
        QVERIFY(empty.canUndo());
        QCOMPARE(empty.revision(), quint64(1));
    }
    void imageEffectsIncludeAllExistingToolsAndKeepNewAnnotationsColored() {
        QImage source = page(181, 139);
        for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
            QColor color = source.pixelColor(x, y);
            color.setAlpha((x * 17 + y * 31) % 256);
            source.setPixelColor(x, y, color);
        }
        for (int tool = 0; tool <= int(Tool::Number); ++tool) {
            for (ImageEffect effect : {ImageEffect::Grayscale, ImageEffect::Invert, ImageEffect::Brightness}) {
                ImageDocument document(source);
                QVERIFY(document.crop(QRect(17, 11, 143, 111)));
                document.flip(Qt::Horizontal); document.flip(Qt::Vertical);
                const Annotation existing{Tool(tool), {{13, 11}, {126, 91}}, QColor(237, 47, 83, 190), 4,
                                          tool == int(Tool::Number) ? "27" : "wrapped text 123 456 789"};
                document.add(existing);
                const QImage composed = document.render();
                const int amount = effect == ImageEffect::Brightness ? 23 : 0;
                document.applyEffect(effect, amount);
                QImage expected = effectImage(composed, effect, amount);
                QCOMPARE(document.render(), expected);
                QCOMPARE(document.annotations(), QVector<Annotation>{existing});
                const Annotation fresh{Tool::Line, {{20, 15}, {105, 15}}, Qt::blue, 6, {}};
                paintAnnotation(expected, fresh);
                document.add(fresh);
                QCOMPARE(document.render(), expected);
                QCOMPARE(document.render().pixelColor(60, 15), QColor(Qt::blue));
                QCOMPARE(document.original(), source);
            }
        }
    }
    void imageEffectsKeepEditedAndRestoredObjectsAtTheirOriginalLayer() {
        const QImage source = page(181, 139);
        const Annotation first{Tool::Line, {{10, 40}, {160, 40}}, Qt::red, 8, {}};
        const Annotation middle{Tool::Highlight, {{10, 80}, {160, 80}}, Qt::yellow, 3, {}};
        const Annotation last{Tool::Mosaic, {{30, 20}, {140, 105}}, Qt::black, 3, {}};
        const auto composed = [&](const std::optional<Annotation> &before, const std::optional<Annotation> &between) {
            QImage result = source;
            if (before) paintAnnotation(result, *before);
            result = effectImage(result, ImageEffect::Brightness, 17);
            result = effectImage(result, ImageEffect::Invert);
            if (between) paintAnnotation(result, *between);
            result = effectImage(result, ImageEffect::Grayscale);
            paintAnnotation(result, last);
            return result;
        };
        ImageDocument document(source);
        document.add(first);
        document.applyEffect(ImageEffect::Brightness, 17);
        document.applyEffect(ImageEffect::Invert);
        document.add(middle);
        document.applyEffect(ImageEffect::Grayscale);
        document.add(last);
        QCOMPARE(document.render(), composed(first, middle));
        Annotation changed = first;
        changed.color = Qt::green;
        for (auto &point : changed.points) point += QPointF(0, 12);
        QCOMPARE(document.render(0, changed), composed(changed, middle));
        QCOMPARE(document.render(), composed(first, middle));
        document.replace(0, changed);
        QCOMPARE(document.render(), composed(changed, middle));
        QCOMPARE(document.hitTest({15, 52}, 0), 0);
        document.remove(0);
        QCOMPARE(document.render(), composed(std::nullopt, middle));
        document.undo(); QCOMPARE(document.render(), composed(changed, middle));
        QCOMPARE(document.annotations().first(), changed);
        document.redo(); QCOMPARE(document.render(), composed(std::nullopt, middle));
        document.undo();
        document.remove(1);
        QCOMPARE(document.render(), composed(changed, std::nullopt));
        document.undo(); QCOMPARE(document.render(), composed(changed, middle));
        QCOMPARE(document.original(), source);
    }
    void imageEffectsAndFrameChangesShareUndoRedoAndBranching() {
        const QImage source = page(221, 181);
        ImageDocument document(source);
        QImage expected = source;
        QVector<QImage> images{source};
        QVector<QVector<Annotation>> objects{{}};
        const auto record = [&] { images.append(expected); objects.append(document.annotations()); };
        const Annotation first{Tool::Rectangle, {{12, 17}, {190, 155}}, Qt::red, 4, {}};
        document.add(first); paintAnnotation(expected, first);
        QCOMPARE(document.render(), expected); record();
        document.applyEffect(ImageEffect::Invert); expected = effectImage(expected, ImageEffect::Invert);
        QCOMPARE(document.render(), expected); record();
        const QRect crop(17, 11, 181, 151);
        QVERIFY(document.crop(crop)); expected = expected.copy(crop);
        QCOMPARE(document.render(), expected); record();
        document.flip(Qt::Horizontal); expected = expected.mirrored(true, false);
        QCOMPARE(document.render(), expected); record();
        const Annotation text{Tool::Text, {{13, 11}}, Qt::blue, 3, "new text 123"};
        document.add(text); paintAnnotation(expected, text);
        QCOMPARE(document.render(), expected); record();
        document.applyEffect(ImageEffect::Grayscale); expected = effectImage(expected, ImageEffect::Grayscale);
        QCOMPARE(document.render(), expected); record();
        document.applyEffect(ImageEffect::Brightness, 21); expected = effectImage(expected, ImageEffect::Brightness, 21);
        QCOMPARE(document.render(), expected); record();
        document.flip(Qt::Vertical); expected = expected.mirrored(false, true);
        QCOMPARE(document.render(), expected); record();
        const QRect nextCrop(7, 9, 121, 103);
        QVERIFY(document.crop(nextCrop)); expected = expected.copy(nextCrop);
        QCOMPARE(document.render(), expected); record();
        document.applyEffect(ImageEffect::Invert); expected = effectImage(expected, ImageEffect::Invert);
        QCOMPARE(document.render(), expected); record();
        const Annotation mosaic{Tool::MosaicPen, {{11, 19}, {105, 87}}, Qt::black, 3, {}};
        document.add(mosaic); paintAnnotation(expected, mosaic);
        QCOMPARE(document.render(), expected); record();
        QCOMPARE(document.revision(), quint64(images.size() - 1));
        for (int i = images.size() - 2; i >= 0; --i) {
            document.undo();
            QCOMPARE(document.render(), images[i]);
            QCOMPARE(document.annotations(), objects[i]);
            QCOMPARE(document.original(), source);
        }
        QVERIFY(!document.canUndo());
        for (int i = 1; i < images.size(); ++i) {
            document.redo();
            QCOMPARE(document.render(), images[i]);
            QCOMPARE(document.annotations(), objects[i]);
        }
        QVERIFY(!document.canRedo());
        document.undo(); document.undo();
        expected = images[images.size() - 3];
        const QImage beforeBranch = expected;
        const Annotation branch{Tool::Line, {{10, 15}, {110, 15}}, Qt::green, 6, {}};
        document.add(branch); paintAnnotation(expected, branch);
        QVERIFY(!document.canRedo());
        QCOMPARE(document.render(), expected);
        document.applyEffect(ImageEffect::Brightness, -23);
        const QImage darkened = effectImage(expected, ImageEffect::Brightness, -23);
        QCOMPARE(document.render(), darkened);
        document.undo(); QCOMPARE(document.render(), expected);
        document.undo(); QCOMPARE(document.render(), beforeBranch);
        document.redo(); document.redo(); QCOMPARE(document.render(), darkened);
        QCOMPARE(document.original(), source);
    }
    void brightnessClipsRgbPreservesAlphaAndTreatsZeroAsIdentity() {
        const QRgb originalPixels[]{qRgba(5, 100, 240, 0), qRgba(250, 1, 30, 64),
                                    qRgba(60, 128, 190, 128), qRgba(0, 255, 127, 255)};
        QImage source(2, 2, QImage::Format_ARGB32);
        for (int i = 0; i < 4; ++i) source.setPixel(i % 2, i / 2, originalPixels[i]);
        struct Sample { int amount; QRgb pixels[4]; };
        for (const Sample &sample : {
                 Sample{20, {qRgba(56, 151, 255, 0), qRgba(255, 52, 81, 64),
                             qRgba(111, 179, 241, 128), qRgba(51, 255, 178, 255)}},
                 Sample{-20, {qRgba(0, 49, 189, 0), qRgba(199, 0, 0, 64),
                              qRgba(9, 77, 139, 128), qRgba(0, 204, 76, 255)}},
                 Sample{1, {qRgba(8, 103, 243, 0), qRgba(253, 4, 33, 64),
                            qRgba(63, 131, 193, 128), qRgba(3, 255, 130, 255)}},
                 Sample{-1, {qRgba(2, 97, 237, 0), qRgba(247, 0, 27, 64),
                             qRgba(57, 125, 187, 128), qRgba(0, 252, 124, 255)}},
                 Sample{0, {originalPixels[0], originalPixels[1], originalPixels[2], originalPixels[3]}},
                 Sample{-100, {qRgba(0, 0, 0, 0), qRgba(0, 0, 0, 64), qRgba(0, 0, 0, 128), qRgba(0, 0, 0, 255)}},
                 Sample{100, {qRgba(255, 255, 255, 0), qRgba(255, 255, 255, 64),
                              qRgba(255, 255, 255, 128), qRgba(255, 255, 255, 255)}}}) {
            ImageDocument document(source);
            document.applyEffect(ImageEffect::Brightness, sample.amount);
            const QImage rendered = document.render();
            for (int i = 0; i < 4; ++i) QCOMPARE(rendered.pixel(i % 2, i / 2), sample.pixels[i]);
            QCOMPARE(document.original(), source);
            QCOMPARE(document.canUndo(), sample.amount != 0);
            QCOMPARE(document.revision(), sample.amount == 0 ? quint64(0) : quint64(1));
            if (sample.amount != 0) {
                document.undo(); QCOMPARE(document.render(), source);
                document.redo(); QCOMPARE(document.render(), rendered);
            }
        }
        for (ImageEffect effect : {ImageEffect::Grayscale, ImageEffect::Invert}) {
            ImageDocument document(source);
            document.applyEffect(effect, 75);
            QCOMPARE(document.render(), effectImage(source, effect));
        }
    }
    void brightnessPreviewLeavesHistoryUntouchedAndMatchesCommit() {
        const QImage source = page(181, 139);
        ImageDocument document(source);
        document.add({Tool::Rectangle, {{12, 17}, {160, 115}}, Qt::red, 4, {}});
        document.applyEffect(ImageEffect::Invert);
        QVERIFY(document.crop(QRect(17, 11, 143, 111)));
        document.flip(Qt::Horizontal);
        document.add({Tool::Text, {{13, 11}}, Qt::blue, 3, "new text 123"});
        document.applyEffect(ImageEffect::Brightness, 17);
        document.flip(Qt::Vertical);
        document.applyEffect(ImageEffect::Brightness, -9);
        document.undo();
        const QImage committed = document.render();
        const auto objects = document.annotations();
        const quint64 revision = document.revision();
        QVERIFY(document.canUndo());
        QVERIFY(document.canRedo());
        for (int amount : {20, -20, 0, 100, -100, 13, 0}) {
            QCOMPARE(document.previewBrightness(amount), effectImage(committed, ImageEffect::Brightness, amount));
            QCOMPARE(document.render(), committed);
            QCOMPARE(document.original(), source);
            QCOMPARE(document.annotations(), objects);
            QCOMPARE(document.revision(), revision);
            QVERIFY(document.canUndo());
            QVERIFY(document.canRedo());
        }
        document.applyEffect(ImageEffect::Brightness, 0);
        QCOMPARE(document.render(), committed);
        QCOMPARE(document.revision(), revision);
        QVERIFY(document.canRedo());
        document.redo(); QCOMPARE(document.render(), effectImage(committed, ImageEffect::Brightness, -9));
        document.undo(); QCOMPARE(document.render(), committed);
        const QImage preview = document.previewBrightness(25);
        const quint64 beforeCommit = document.revision();
        document.applyEffect(ImageEffect::Brightness, 25);
        QCOMPARE(document.render(), preview);
        QCOMPARE(document.revision(), beforeCommit + 1);
        QVERIFY(!document.canRedo());
        QCOMPARE(document.annotations(), objects);
        QCOMPARE(document.original(), source);
        document.undo(); QCOMPARE(document.render(), committed);
        document.redo(); QCOMPARE(document.render(), preview);
    }
    void wordsHitTestUsesSourceCoordinates() {
        const QVector<OcrWord> words{{QRectF(10, 20, 50, 15), 0, 5}, {QRectF(65, 20, 50, 15), 6, 5}};
        QCOMPARE(wordAt(words, QPointF(15, 25)), 0);
        QCOMPARE(wordAt(words, QPointF(70, 25)), 1);
        QCOMPARE(wordAt(words, QPointF(200, 200)), -1);
    }
    void textDragUsesDistanceToRectangle() {
        const QVector<OcrWord> words{{QRectF(0, 0, 200, 20), 0, 4}, {QRectF(205, 30, 10, 10), 5, 5}};
        QCOMPARE(wordAt(words, QPointF(198, 25)), -1);
        QCOMPARE(wordAt(words, QPointF(198, 25), true), 0);
        QCOMPARE(wordAt(words, QPointF(205, 22), true), 0);
        QCOMPARE(wordAt(words, QPointF(210, 35), true), 1);
        QCOMPARE(wordAt({}, QPointF(10, 10), true), -1);
    }
    void textSelectionIsOrderedAcrossLines() {
        const QString text = QStringLiteral("  hello  world,\t你好\r\n世界🙂 e\u0301  ");
        const OcrResult result{text, {{{}, text.indexOf("hello"), 5}, {{}, text.indexOf("world"), 5},
            {{}, text.indexOf(QStringLiteral("你好")), 2}, {{}, text.indexOf(QStringLiteral("世界")), 4},
            {{}, text.indexOf(QStringLiteral("e\u0301")), 2}}, {}};
        QCOMPARE(selectedText(result, 0, 3), QStringLiteral("hello  world,\t你好\r\n世界🙂"));
        QCOMPARE(selectedText(result, 3, 0), selectedText(result, 0, 3));
        QCOMPARE(selectedText(result, 1, 1), QString("world"));
        QCOMPARE(selectedText(result, 3, 3), QStringLiteral("世界🙂"));
        QCOMPARE(selectedText(result, 4, 4), QStringLiteral("e\u0301"));
        QVERIFY(selectedText(result, -1, 2).isEmpty());
        QVERIFY(selectedText(result, 0, 5).isEmpty());
    }
    void ocrParsingKeepsGeometryAndClips() {
        const QByteArray json = R"({"ok":true,"text":"  word  ","words":[{"start":2,"length":4,"x":90,"y":10,"width":30,"height":20},{"start":2,"length":4,"x":200,"y":10,"width":30,"height":20},{"start":2,"length":4,"width":-1}]})";
        const auto result = parseOcr(json, QSize(100, 100));
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.words.size(), 1);
        QCOMPARE(result.words[0].bounds, QRectF(90, 10, 10, 20));
        QCOMPARE(result.text, QString("  word  "));
        QCOMPARE(result.words[0].textStart, 2);
        QCOMPARE(result.words[0].textLength, 4);
        QCOMPARE(selectedText(result, 0, 0), QString("word"));
    }
    void ocrParsingPreservesRepeatedWordsAndUnicodeRanges() {
        const auto result = parseOcr(R"({"ok":true,"text":"  one  one,\t\u4e2d\ud83d\ude42\r\nB  ","words":[
            {"start":2,"length":3,"x":0,"y":0,"width":20,"height":10},
            {"start":7,"length":3,"x":30,"y":0,"width":20,"height":10},
            {"start":12,"length":3,"x":60,"y":0,"width":20,"height":10},
            {"start":17,"length":1,"x":0,"y":20,"width":20,"height":10}]})", QSize(100, 100));
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.text, QStringLiteral("  one  one,\t中🙂\r\nB  "));
        QCOMPARE(result.words.size(), 4);
        QCOMPARE(selectedText(result, 1, 3), QStringLiteral("one,\t中🙂\r\nB"));
        QCOMPARE(selectedText(result, 3, 1), selectedText(result, 1, 3));
        QCOMPARE(selectedText(result, 2, 2), QStringLiteral("中🙂"));
    }
    void ocrParsingSkipsInvalidTextRanges() {
        const auto result = parseOcr(R"({"ok":true,"text":"ab\ud83d\ude42cd","words":[
            {"start":-1,"length":1,"width":10,"height":10},
            {"start":0,"length":0,"width":10,"height":10},
            {"start":5,"length":2,"width":10,"height":10},
            {"start":1.5,"length":1,"width":10,"height":10},
            {"start":9223372036854775807,"length":1,"width":10,"height":10},
            {"start":2,"length":2,"width":10,"height":10},
            {"start":0,"length":1,"width":10,"height":10},
            {"start":4,"length":2,"width":10,"height":10}]})", QSize(100, 100));
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.words.size(), 2);
        QCOMPARE(selectedText(result, 0, 1), QStringLiteral("🙂cd"));
        const auto textOnly = parseOcr(R"({"ok":true,"text":"text without geometry","words":[]})", QSize(100, 100));
        QCOMPARE(textOnly.text, QString("text without geometry"));
        QVERIFY(textOnly.words.isEmpty());
        QVERIFY(textOnly.error.isEmpty());
    }
    void ocrFailuresAreVisible() {
        QVERIFY(!parseOcr("not json", QSize(100, 100)).error.isEmpty());
        QCOMPARE(parseOcr(R"({"ok":false,"message":"missing language pack"})", QSize(100, 100)).error, QString("missing language pack"));
        QVERIFY(!parseOcr(R"({"ok":true,"words":[]})", QSize(100, 100)).error.isEmpty());
        QVERIFY(!parseOcr(R"({"ok":true,"text":true,"words":[]})", QSize(100, 100)).error.isEmpty());
        QVERIFY(!parseOcr(R"({"ok":true,"text":"text","words":{}})", QSize(100, 100)).error.isEmpty());
    }
    void stitchingPreservesEveryPixel() {
        const auto source = page();
        const auto a = source.copy(0, 0, 60, 100);
        const auto b = source.copy(0, 40, 60, 100);
        const auto c = source.copy(0, 80, 60, 100);
        QCOMPARE(findOverlap(a, b), 60);
        QCOMPARE(appendScrollFrame(a, b), source.copy(0, 0, 60, 140));
        QCOMPARE(appendScrollFrame(appendScrollFrame(a, b), c), source.copy(0, 0, 60, 180));
    }
    void identicalFrameAddsNothing() {
        const auto image = page();
        QCOMPARE(appendScrollFrame(image, image), image);
    }
    void stitchingChecksLimitsBeforeAllocatingOutput() {
        const auto source = page();
        const auto a = source.copy(0, 0, 60, 100), b = source.copy(0, 40, 60, 100);
        bool unmatched = true;
        QVERIFY(appendScrollFrame(a, b, 139, 256LL * 1024 * 1024, &unmatched).isNull());
        QVERIFY(!unmatched);
        QVERIFY(appendScrollFrame(a, b, 140, 60 * 140 * 4 - 1).isNull());
        QCOMPARE(appendScrollFrame(a, b, 140, 60 * 140 * 4), source.copy(0, 0, 60, 140));
        QVERIFY(appendScrollFrame({}, a, 99).isNull());
        QCOMPARE(findOverlap(a.convertToFormat(QImage::Format_RGB888), b), 60);
    }
    void widthMismatchAndMissingOverlapAreRejected() {
        QVERIFY(appendScrollFrame(page(40), page(60)).isNull());
        QImage white(60, 100, QImage::Format_RGB32);
        white.fill(Qt::white);
        QImage black = white;
        black.fill(Qt::black);
        bool unmatched = false;
        QVERIFY(appendScrollFrame(white, black, 24000, 256LL * 1024 * 1024, &unmatched).isNull());
        QVERIFY(unmatched);
        QVERIFY(appendScrollFrame(white, {}, 24000, 256LL * 1024 * 1024, &unmatched).isNull());
        QVERIFY(!unmatched);
    }
    void staticBandCannotConsumeMovingContent() {
        QImage a = page(60, 100);
        QImage b = page(60, 100).mirrored();
        for (int y = 0; y < 80; ++y) for (int x = 0; x < 60; ++x) b.setPixelColor(x, y, Qt::white);
        for (int y = 20; y < 100; ++y) for (int x = 0; x < 60; ++x) a.setPixelColor(x, y, Qt::white);
        QCOMPARE(findOverlap(a, b), -1);
    }
    void imageSavesAndReloads() {
        QTemporaryDir dir;
        QString error;
        const QString path = dir.filePath("中文截图.png");
        const auto image = page();
        QVERIFY2(saveImage(image, path, &error), qPrintable(error));
        QCOMPARE(QImage(path), image);
        QVERIFY(!QFile::exists(path + ".tmp"));
    }
    void failedSaveDoesNotDestroyExistingFile() {
        QTemporaryDir dir;
        const auto path = dir.filePath("keep.png");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("original");
        file.close();
        QString error;
        QVERIFY(!saveImage({}, path, &error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original"));
    }
    void settingsUseAtomicJson() {
        QTemporaryDir dir;
        const auto path = dir.filePath("settings.json");
        const QJsonObject object{{"hotkeys", QJsonObject{{"capture", "Ctrl+1"}}}, {"language", QStringLiteral("中文")}};
        QVERIFY(writeJson(path, object));
        QCOMPARE(readJson(path), object);
    }
    void dataDirectoryOverridesAndImageNamesUseStill() {
        QTemporaryDir dir;
        const QByteArray current = qgetenv("STILL_DATA_DIR"), legacy = qgetenv("PIXCLONE_DATA_DIR");
        const auto restore = qScopeGuard([=] {
            if (current.isNull()) qunsetenv("STILL_DATA_DIR"); else qputenv("STILL_DATA_DIR", current);
            if (legacy.isNull()) qunsetenv("PIXCLONE_DATA_DIR"); else qputenv("PIXCLONE_DATA_DIR", legacy);
        });
        qunsetenv("STILL_DATA_DIR");
        qputenv("PIXCLONE_DATA_DIR", dir.filePath("old").toUtf8());
        QCOMPARE(dataDirectory(), dir.filePath("old"));
        qputenv("STILL_DATA_DIR", dir.filePath("new").toUtf8());
        QCOMPARE(dataDirectory(), dir.filePath("new"));
        QVERIFY(QFileInfo(uniqueImagePath(dir.path())).fileName().startsWith("still_"));
    }
    void legacyRoamingDataDirectoryIsReused() {
#ifdef Q_OS_WIN
        QTemporaryDir dir;
        const QByteArray previousLocal = qgetenv("LOCALAPPDATA");
        const QByteArray previousRoaming = qgetenv("APPDATA");
        const QByteArray previousOverride = qgetenv("STILL_DATA_DIR");
        const QByteArray previousLegacyOverride = qgetenv("PIXCLONE_DATA_DIR");
        const auto restore = qScopeGuard([=] {
            qputenv("LOCALAPPDATA", previousLocal);
            qputenv("APPDATA", previousRoaming);
            if (previousOverride.isNull()) qunsetenv("STILL_DATA_DIR"); else qputenv("STILL_DATA_DIR", previousOverride);
            if (previousLegacyOverride.isNull()) qunsetenv("PIXCLONE_DATA_DIR"); else qputenv("PIXCLONE_DATA_DIR", previousLegacyOverride);
        });
        qputenv("LOCALAPPDATA", dir.filePath("local").toUtf8());
        qputenv("APPDATA", dir.filePath("roaming").toUtf8());
        qunsetenv("STILL_DATA_DIR");
        qunsetenv("PIXCLONE_DATA_DIR");
        const QString expected = dir.filePath("roaming/PixClone");
        QVERIFY(writeJson(QDir(expected).filePath("settings.json"), {{"version", 1}}));
        QCOMPARE(dataDirectory(), expected);
        const QString oldLocal = dir.filePath("local/PixClone"), newLocal = dir.filePath("local/Still");
        QVERIFY(writeJson(QDir(oldLocal).filePath("Clipboard/index.json"), {{"version", 1}}));
        QCOMPARE(dataDirectory(), oldLocal);
        QVERIFY(writeJson(QDir(newLocal).filePath("qt-settings.json"), {{"version", 1}}));
        QCOMPARE(dataDirectory(), newLocal);
        QVERIFY(QFile::remove(QDir(newLocal).filePath("qt-settings.json")));
        QVERIFY(writeJson(QDir(newLocal).filePath("PinSessions/session.json"), {{"version", 1}}));
        QCOMPARE(dataDirectory(), newLocal);
#endif
    }
    void historyCanReadLegacyFilesWithoutRewritingIndex() {
        QTemporaryDir dir;
        const QByteArray before = qgetenv("STILL_DATA_DIR");
        qputenv("STILL_DATA_DIR", dir.path().toUtf8());
        QDir().mkpath(dir.filePath("History"));
        const auto index = dir.filePath("History/history.json");
        QFile file(index);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("legacy index");
        file.close();
        QString error;
        QVERIFY(!addHistory(page(), 2, &error).isEmpty());
        QVERIFY(!addHistory(page(), 2, &error).isEmpty());
        QVERIFY(!addHistory(page(), 2, &error).isEmpty());
        QCOMPARE(historyFiles().size(), 2);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("legacy index"));
        if (before.isNull()) qunsetenv("STILL_DATA_DIR"); else qputenv("STILL_DATA_DIR", before);
    }
};

QTEST_MAIN(CoreTest)
#include "core_test.moc"
