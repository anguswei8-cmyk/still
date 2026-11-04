#include "imaging/image.h"
#include "storage/store.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>

using namespace pixclone;

static QImage page(int width = 60, int height = 360) {
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        image.setPixel(x, y, qRgb((y * 31 + x * 7) % 256, (y * 17 + x * 3) % 256, (y * 53 + x * 11) % 256));
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
    void wordsHitTestUsesSourceCoordinates() {
        const QVector<OcrWord> words{{"hello", QRectF(10, 20, 50, 15), 0}, {"world", QRectF(65, 20, 50, 15), 0}};
        QCOMPARE(wordAt(words, QPointF(15, 25)), 0);
        QCOMPARE(wordAt(words, QPointF(70, 25)), 1);
        QCOMPARE(wordAt(words, QPointF(200, 200)), -1);
    }
    void textSelectionIsOrderedAcrossLines() {
        const QVector<OcrWord> words{{"hello", {}, 0}, {"world", {}, 0}, {QStringLiteral("你好"), {}, 1}, {QStringLiteral("世界"), {}, 1}};
        QCOMPARE(selectedText(words, 0, 3), QStringLiteral("hello world\n你好世界"));
        QCOMPARE(selectedText(words, 3, 0), selectedText(words, 0, 3));
        QCOMPARE(selectedText(words, 1, 1), QString("world"));
        QVERIFY(selectedText(words, -1, 2).isEmpty());
        QVERIFY(selectedText(words, 0, 4).isEmpty());
    }
    void ocrParsingKeepsGeometryAndClips() {
        const QByteArray json = R"({"ok":true,"words":[{"text":"word","x":90,"y":10,"width":30,"height":20,"line":2},{"text":"bad","x":200,"y":10,"width":30,"height":20},{"text":"bad","width":-1}]})";
        const auto result = parseOcr(json, QSize(100, 100));
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.words.size(), 1);
        QCOMPARE(result.words[0].bounds, QRectF(90, 10, 10, 20));
        QCOMPARE(result.words[0].line, 2);
    }
    void ocrFailuresAreVisible() {
        QVERIFY(!parseOcr("not json", QSize(100, 100)).error.isEmpty());
        QCOMPARE(parseOcr(R"({"ok":false,"message":"missing language pack"})", QSize(100, 100)).error, QString("missing language pack"));
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
    void legacyRoamingDataDirectoryIsReused() {
#ifdef Q_OS_WIN
        QTemporaryDir dir;
        const QByteArray previousLocal = qgetenv("LOCALAPPDATA");
        const QByteArray previousRoaming = qgetenv("APPDATA");
        const QByteArray previousOverride = qgetenv("PIXCLONE_DATA_DIR");
        const auto restore = qScopeGuard([=] {
            qputenv("LOCALAPPDATA", previousLocal);
            qputenv("APPDATA", previousRoaming);
            if (previousOverride.isNull()) qunsetenv("PIXCLONE_DATA_DIR"); else qputenv("PIXCLONE_DATA_DIR", previousOverride);
        });
        qputenv("LOCALAPPDATA", dir.filePath("local").toUtf8());
        qputenv("APPDATA", dir.filePath("roaming").toUtf8());
        qunsetenv("PIXCLONE_DATA_DIR");
        const QString expected = dir.filePath("roaming/PixClone");
        QVERIFY(writeJson(QDir(expected).filePath("settings.json"), {{"version", 1}}));
        QCOMPARE(dataDirectory(), expected);
#endif
    }
    void historyCanReadLegacyFilesWithoutRewritingIndex() {
        QTemporaryDir dir;
        const QByteArray before = qgetenv("PIXCLONE_DATA_DIR");
        qputenv("PIXCLONE_DATA_DIR", dir.path().toUtf8());
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
        if (before.isNull()) qunsetenv("PIXCLONE_DATA_DIR"); else qputenv("PIXCLONE_DATA_DIR", before);
    }
};

QTEST_MAIN(CoreTest)
#include "core_test.moc"
