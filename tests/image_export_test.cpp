#include "storage/image_export.h"
#include "storage/store.h"
#include "clipboard/image_copy.h"
#include "clipboard/clipboard_data.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QImageWriter>
#include <QSet>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QtConcurrentRun>

using namespace still;

static QImage picture() {
    QImage image(320, 240, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        image.setPixel(x, y, qRgb(x % 256, y % 256, (x * y) % 256));
    return image;
}

class ImageExportTest : public QObject {
    Q_OBJECT
private slots:
    void copyingDefaultsToTheOriginalImage() {
        const QImage image = picture();
        QString error;
        QVERIFY(copyImageToClipboard(image, {}, &error));
        QCOMPARE(QGuiApplication::clipboard()->image(), image);
        QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasUrls());
        QGuiApplication::clipboard()->clear();
    }
    void copyingAsFileKeepsAnEncodedFileAfterClipboardChanges() {
        QTemporaryDir saved;
        QStringList copies;
        const auto cleanup = qScopeGuard([&] { QGuiApplication::clipboard()->clear(); for (const auto &path : copies) QDir(QFileInfo(path).absolutePath()).removeRecursively(); });
        QString error;
        for (const auto &format : {"png", "jpeg"}) {
            const QJsonObject options{{"dir", saved.path()}, {"nameTemplate", "clip"}, {"format", format}, {"copyAsFile", true}};
            QVERIFY(copyImageToClipboard(picture(), options, &error));
            const auto *mime = QGuiApplication::clipboard()->mimeData();
            QCOMPARE(mime->urls().size(), 1); QVERIFY(!mime->hasImage());
            const QString path = mime->urls().first().toLocalFile(); copies.append(path);
            QVERIFY(!path.startsWith(saved.path()));
            QCOMPARE(QFileInfo(path).fileName(), format == QString("jpeg") ? QString("clip.jpg") : QString("clip.png"));
            QCOMPARE(QImageReader::imageFormat(path), QByteArray(format));
            if (format == QString("png")) QCOMPARE(QImage(path), picture());
#ifdef Q_OS_WIN
            QCOMPARE(mime->data("application/x-qt-windows-mime;value=\"Preferred DropEffect\""), QByteArray::fromHex("01000000"));
#endif
            ClipEntry entry;
            const auto bytes = encodeClipboard(captureClipboard(*mime), &entry, &error);
            QCOMPARE(entry.kind, QString("files")); QVERIFY(!bytes.isEmpty());
            const auto restored = decodeClipboard(bytes, &error);
            QCOMPARE(restored.urls, mime->urls()); QVERIFY(missingClipboardFiles(restored).isEmpty());
            QGuiApplication::clipboard()->setText("next item");
            QVERIFY(!QImage(path).isNull());
        }
        QVERIFY(QDir(saved.path()).entryList(QDir::Files).isEmpty());
        QVERIFY(copies[0] != copies[1]);
    }
    void failedFileCopyPreservesThePreviousClipboard() {
        QGuiApplication::clipboard()->setText("keep clipboard");
        QString error;
        QVERIFY(!copyImageToClipboard({}, {{"copyAsFile", true}}, &error));
        QVERIFY(!error.isEmpty()); QCOMPARE(QGuiApplication::clipboard()->text(), QString("keep clipboard"));
        QGuiApplication::clipboard()->clear();
    }
    void fileNamesExpandKnownVariables() {
        const QDateTime when(QDate(2026, 11, 6), QTime(9, 8, 7, 6));
        QCOMPARE(imageExportFileName({}, when), QString("still_20261106_090807_006.png"));
        QCOMPARE(imageExportFileName({{"nameTemplate", "shot_{time}_{date}"}, {"format", "jpeg"}}, when), QString("shot_090807_20261106.jpg"));
        QVERIFY(validImageNameTemplate(defaultImageNameTemplate));
        QVERIFY(validImageNameTemplate(QStringLiteral("截图_{date}")));
        for (const auto &pattern : {"", "../shot", "a/b", "a\\b", "{unknown}", "CON", "nul.report", "a:b", ".", "..", "trailing."})
            QVERIFY2(!validImageNameTemplate(pattern), pattern);
    }
    void existingFilesAreNeverOverwritten() {
        QTemporaryDir dir;
        QFile original(dir.filePath("shot.png")); QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("keep original"); original.close();
        const QJsonObject options{{"dir", dir.path()}, {"nameTemplate", "shot"}};
        QString error;
        QCOMPARE(exportImage(picture(), options, &error), dir.filePath("shot-2.png"));
        QCOMPARE(exportImage(picture(), options, &error), dir.filePath("shot-3.png"));
        QVERIFY(error.isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), QByteArray("keep original"));
        QCOMPARE(QImage(dir.filePath("shot-2.png")), picture());
        QVERIFY(QDir(dir.path()).entryList({".still-export-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void concurrentExportsKeepEveryImage() {
        QTemporaryDir dir;
        for (int round = 0; round < 4; ++round) {
            const QJsonObject options{{"dir", dir.filePath(QString::number(round))}, {"nameTemplate", "shot"}};
            QList<QFuture<QPair<QString, QImage>>> saves;
            for (int i = 0; i < 12; ++i) saves.append(QtConcurrent::run([=] {
                QImage image = picture(); image.setPixelColor(0, 0, QColor(i * 20, round * 40, 123));
                QString error;
                return qMakePair(exportImage(image, options, &error), image);
            }));
            QSet<QString> paths;
            for (auto &save : saves) { const auto result = save.result(); QVERIFY(!result.first.isEmpty()); paths.insert(result.first); QCOMPARE(QImage(result.first), result.second); }
            QCOMPARE(paths.size(), 12);
        }
    }
    void danglingLinkIsAnOccupiedName() {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;
        QVERIFY(QFile::link(dir.filePath("missing.png"), dir.filePath("shot.png")));
        QString error;
        QCOMPARE(exportImage(picture(), {{"dir", dir.path()}, {"nameTemplate", "shot"}}, &error), dir.filePath("shot-2.png"));
        QVERIFY(QFileInfo(dir.filePath("shot.png")).isSymLink());
        QVERIFY(!QFileInfo::exists(dir.filePath("missing.png")));
#else
        QSKIP("Unix symbolic links");
#endif
    }
    void qualityAndCompressionAffectEncoding() {
        QTemporaryDir dir;
        const QImage image = picture();
        QString error;
        QJsonObject options{{"dir", dir.path()}, {"nameTemplate", "low"}, {"format", "jpeg"}, {"jpegQuality", 10}};
        const QString low = exportImage(image, options, &error);
        options["nameTemplate"] = "high"; options["jpegQuality"] = 100;
        const QString high = exportImage(image, options, &error);
        QVERIFY(!low.isEmpty() && !high.isEmpty());
        QCOMPARE(QImageReader::imageFormat(high), QByteArray("jpeg"));
        QVERIFY(QFileInfo(high).size() > QFileInfo(low).size());
        options["format"] = "png"; options["nameTemplate"] = "fast"; options["pngCompression"] = 0;
        const QString fast = exportImage(image, options, &error);
        options["nameTemplate"] = "small"; options["pngCompression"] = 100;
        const QString small = exportImage(image, options, &error);
        QVERIFY(!fast.isEmpty() && !small.isEmpty());
        QVERIFY(QFileInfo(fast).size() > QFileInfo(small).size());
        QCOMPARE(QImage(fast), image); QCOMPARE(QImage(small), image);
    }
    void failedExportsLeaveNoPartialFile() {
        QTemporaryDir dir;
        QString error;
        QVERIFY(exportImage({}, {{"dir", dir.path()}, {"nameTemplate", "empty"}}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
        QFile blocked(dir.filePath("blocked")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("keep"); blocked.close();
        error.clear();
        QVERIFY(exportImage(picture(), {{"dir", blocked.fileName()}}, &error).isEmpty());
        QVERIFY(!error.isEmpty()); QVERIFY(blocked.open(QIODevice::ReadOnly)); QCOMPARE(blocked.readAll(), QByteArray("keep"));
    }
    void availableWritersAreRecorded() {
        qInfo() << "Available image writers:" << QImageWriter::supportedImageFormats();
        QVERIFY(QImageWriter::supportedImageFormats().contains("png"));
        QVERIFY(QImageWriter::supportedImageFormats().contains("jpeg"));
    }
};
QTEST_MAIN(ImageExportTest)
#include "image_export_test.moc"
