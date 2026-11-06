#include "clipboard/clipboard_archive.h"
#include "clipboard/clipboard_data.h"
#include "tracked_image.h"

#include <QDir>
#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>

using namespace still;

static QJsonObject options(const QString &root) { return {{"dir", root}, {"maxItems", 200}, {"maxMiB", 256}}; }
static ClipPayload text(const QString &value) { QMimeData mime; mime.setText(value); return captureClipboard(mime); }

class ClipboardTest : public QObject {
    Q_OBJECT
private slots:
    void internalClipboardMarkersAreNotArchived() {
        QMimeData mime;
        mime.setText("keep text");
        mime.setData("application/x-still-clipboard-id", "current");
        mime.setData("application/x-pixclone-clipboard-id", "legacy");
        QString error;
        ClipEntry entry;
        const auto payload = decodeClipboard(encodeClipboard(captureClipboard(mime), &entry, &error), &error);
        std::unique_ptr<QMimeData> restored(restoreClipboard(payload));
        QCOMPARE(restored->text(), QString("keep text"));
        QVERIFY(!restored->hasFormat("application/x-still-clipboard-id"));
        QVERIFY(!restored->hasFormat("application/x-pixclone-clipboard-id"));
    }
    void legacyArchiveMarkerKeepsHistoryReadable() {
        QTemporaryDir directory;
        const QString root = directory.filePath("clips");
        {
            ClipboardArchive archive(options(root));
            archive.add(text("keep history"));
            QTRY_COMPARE(archive.entries().size(), 1);
        }
        QVERIFY(QFile::rename(QDir(root).filePath(".still-clipboard"), QDir(root).filePath(".pixclone-clipboard")));
        ClipboardArchive archive(options(root));
        QTRY_COMPARE(archive.entries().size(), 1);
        QVERIFY(QFile::exists(QDir(root).filePath(".still-clipboard")));
        QSignalSpy ready(&archive, &ClipboardArchive::ready);
        archive.load(archive.entries().first().id);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<ClipPayload>(ready[0][1]).text, QString("keep history"));
    }
    void richTextAndCustomFormatsSurviveRoundTrip() {
        QMimeData mime;
        mime.setText(QStringLiteral("你好 Qt"));
        mime.setHtml(QStringLiteral("<b>你好 Qt</b>"));
        mime.setData("text/rtf", "{\\rtf1 hello}");
        mime.setData("application/x-example", QByteArray::fromHex("000102ff"));
        QString error; ClipEntry entry;
        const auto encoded = encodeClipboard(captureClipboard(mime), &entry, &error);
        QVERIFY2(!encoded.isEmpty(), qPrintable(error));
        const auto payload = decodeClipboard(encoded, &error);
        std::unique_ptr<QMimeData> restored(restoreClipboard(payload));
        QCOMPARE(restored->text(), mime.text());
        QCOMPARE(restored->html(), mime.html());
        QCOMPARE(restored->data("text/rtf"), mime.data("text/rtf"));
        QCOMPARE(restored->data("application/x-example"), mime.data("application/x-example"));
        QCOMPARE(entry.kind, QString("rich"));
    }
    void htmlOnlyFallbackDoesNotReadExternalResources() {
        QTemporaryDir directory;
        QFile stylesheet(directory.filePath("private.css")); QVERIFY(stylesheet.open(QIODevice::WriteOnly));
        stylesheet.write("@import url('probe://local-file-was-read'); p { color: red; }"); stylesheet.close();
        const QString local = QUrl::fromLocalFile(stylesheet.fileName()).toString();
        const QString html = QStringLiteral("<html><head><title>Private title</title>"
            "<link rel=\"stylesheet\" href=\"https://example.invalid/external.css\">"
            "<link rel=\"stylesheet\" href=\"%1\">"
            "<style>@import url('https://example.invalid/import.css'); @import url('%1');"
            "p { background-image: url('https://example.invalid/background.png'); }</style>"
            "</head><body background=\"%1\"><p><b>可见正文</b></p>"
            "<img src=\"https://example.invalid/image.png\"><img src=\"%1\"></body></html>").arg(local);
        int requestedResources = 0;
        const auto previousProvider = QTextDocument::defaultResourceProvider();
        const auto restoreProvider = qScopeGuard([&] { QTextDocument::setDefaultResourceProvider(previousProvider); });
        QTextDocument::setDefaultResourceProvider([&](const QUrl &) { ++requestedResources; return QVariant(); });
        QMimeData mime; mime.setHtml(html);
        QString error; ClipEntry entry;
        const auto captured = captureClipboard(mime, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(captured.text.isEmpty()); QCOMPARE(requestedResources, 0);
        const auto encoded = encodeClipboard(captured, &entry, &error);
        QVERIFY2(!encoded.isEmpty(), qPrintable(error)); QCOMPARE(entry.kind, QString("rich"));
        QVERIFY(entry.preview.contains(QStringLiteral("可见正文"))); QVERIFY(!entry.preview.contains("Private title"));
        QCOMPARE(requestedResources, 0);
        const auto decoded = decodeClipboard(encoded, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(decoded.text.contains(QStringLiteral("可见正文")));
        QCOMPARE(requestedResources, 0);
        QCOMPARE(decoded.formats.size(), captured.formats.size());
        for (qsizetype i = 0; i < captured.formats.size(); ++i) {
            QCOMPARE(decoded.formats[i].name, captured.formats[i].name);
            QCOMPARE(decoded.formats[i].data, captured.formats[i].data);
        }
        std::unique_ptr<QMimeData> restored(restoreClipboard(decoded)); QCOMPARE(restored->html(), html);
    }
    void fileHistoryStoresReferencesAndRestoresCopyOperation() {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("中文文件.txt"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("content"); file.close();
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path)});
        mime.setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", QByteArray::fromHex("02000000"));
        QString error; ClipEntry entry;
        const auto payload = decodeClipboard(encodeClipboard(captureClipboard(mime), &entry, &error), &error);
        std::unique_ptr<QMimeData> restored(restoreClipboard(payload));
        QCOMPARE(restored->urls(), mime.urls());
        QCOMPARE(entry.kind, QString("files"));
        QVERIFY(missingClipboardFiles(payload).isEmpty());
#ifdef Q_OS_WIN
        QCOMPARE(restored->data("application/x-qt-windows-mime;value=\"Preferred DropEffect\""), QByteArray::fromHex("01000000"));
#endif
        QVERIFY(QFile::remove(path));
        QVERIFY(!missingClipboardFiles(payload).isEmpty());
    }
    void imagesPreservePixelsAndHaveSmallPreview() {
        QImage image(300, 200, QImage::Format_ARGB32); image.fill(QColor(12, 34, 56, 100));
        QMimeData mime; mime.setImageData(image);
        QString error; ClipEntry entry;
        const auto payload = decodeClipboard(encodeClipboard(captureClipboard(mime), &entry, &error), &error);
        QCOMPARE(payload.image, image);
        QCOMPARE(entry.thumbnail.width(), 128);
        QVERIFY(entry.thumbnail.height() <= 96);
        QCOMPARE(entry.kind, QString("image"));
    }
    void imagePixelBudgetMatchesRestoreBudget_data() {
        QTest::addColumn<int>("format");
        QTest::addColumn<int>("height");
        for (const auto format : {QImage::Format_Grayscale8, QImage::Format_Mono}) {
            const QByteArray name = QByteArray::number(format);
            QTest::newRow((name + "-at-limit").constData()) << int(format) << 4096;
            QTest::newRow((name + "-over-limit").constData()) << int(format) << 4097;
        }
    }
    void galleryPreviewsStayInsideIndexBudgetAndReload() {
        QTemporaryDir directory;
        QImage image(256, 192, QImage::Format_ARGB32);
        quint32 random = 0x713a52;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            image.setPixel(x, y, qRgb(random & 255, (random >> 8) & 255, (random >> 16) & 255));
        }
        QImage preview;
        const auto config = options(directory.filePath("clips"));
        {
            ClipboardArchive archive(config);
            ClipPayload payload; payload.image = image; archive.add(std::move(payload));
            QTRY_COMPARE(archive.entries().size(), 1);
            preview = archive.entries().first().thumbnail;
            QVERIFY(!preview.isNull()); QVERIFY(preview.width() <= 128 && preview.height() <= 96);
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(preview.save(&buffer, "PNG"));
            QVERIFY(bytes.size() <= 17000); QVERIFY(bytes.toBase64().size() < 24000);
        }
        ClipboardArchive restored(config); QTRY_COMPARE(restored.entries().size(), 1);
        QCOMPARE(restored.entries().first().thumbnail.convertToFormat(QImage::Format_RGBA8888), preview.convertToFormat(QImage::Format_RGBA8888));
    }
    void imagePixelBudgetMatchesRestoreBudget() {
        QFETCH(int, format);
        QFETCH(int, height);
        QImage image(8192, height, QImage::Format(format));
        if (format == QImage::Format_Mono) image.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        image.fill(0);
        QMimeData mime; mime.setImageData(image);
        QString error; ClipEntry entry;
        const auto captured = captureClipboard(mime, &error);
        if (height > 4096) {
            QVERIFY(captured.empty());
            QVERIFY(!error.isEmpty());
            error.clear();
            ClipPayload direct; direct.image = image;
            QVERIFY(encodeClipboard(direct, &entry, &error).isEmpty());
            QVERIFY(!error.isEmpty());
        } else {
            QVERIFY2(!captured.empty(), qPrintable(error));
            const auto encoded = encodeClipboard(captured, &entry, &error);
            QVERIFY2(!encoded.isEmpty(), qPrintable(error));
            const auto restored = decodeClipboard(encoded, &error);
            QVERIFY2(!restored.image.isNull(), qPrintable(error));
            QCOMPARE(restored.image.size(), image.size());
            QCOMPARE(restored.image.pixelColor(0, 0), image.pixelColor(0, 0));
        }
    }
    void archivedImageReleasesItsOriginalPixels() {
        QTemporaryDir directory;
        ClipboardArchive archive(options(directory.filePath("clips")));
        ClipPayload payload; payload.image = trackedImage(1600, 900);
        QCOMPARE(liveTrackedImages.load(), 1);
        archive.add(std::move(payload));
        QTRY_COMPARE(archive.entries().size(), 1);
        QTRY_COMPARE(liveTrackedImages.load(), 0);
        QVERIFY(archive.entries().first().thumbnail.width() <= 128);
    }
    void exclusionsAndCorruptPayloadsAreRejected() {
        QMimeData mime; mime.setText("secret");
        mime.setData("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\"", QByteArray::fromHex("00000000"));
        QVERIFY(captureClipboard(mime).empty());
        mime.removeFormat("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\"");
        mime.setData("x-kde-passwordManagerHint", "secret");
        QVERIFY(captureClipboard(mime).empty());
        QString error;
        QVERIFY(decodeClipboard("not json", &error).empty());
        QVERIFY(!error.isEmpty());
    }
    void persistenceDedupPinAndOrderedClear() {
        QTemporaryDir directory;
        const auto config = options(directory.filePath("clips"));
        QString id;
        {
            ClipboardArchive archive(config);
            QSignalSpy changed(&archive, &ClipboardArchive::changed);
            archive.add(text("first"));
            QTRY_COMPARE(archive.entries().size(), 1);
            id = archive.entries().first().id;
            archive.pin(id, true);
            archive.add(text("first"));
            archive.add(text("second"));
            archive.clear();
            QTRY_VERIFY(changed.count() >= 6);
            QCOMPARE(archive.entries().size(), 1);
            QVERIFY(archive.entries().first().pinned);
        }
        ClipboardArchive archive(config);
        QTRY_COMPARE(archive.entries().size(), 1);
        QSignalSpy ready(&archive, &ClipboardArchive::ready);
        archive.load(id);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<ClipPayload>(ready[0][1]).text, QString("first"));
        archive.clear(true);
        QTRY_VERIFY(archive.entries().isEmpty());
    }
    void malformedIndexPreservesPayloadsAndBlocksWrites_data() {
        QTest::addColumn<QString>("damage");
        for (const auto name : {"entries-object", "missing-entries", "non-object-row", "invalid-id", "invalid-created"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void malformedIndexPreservesPayloadsAndBlocksWrites() {
        QFETCH(QString, damage);
        QTemporaryDir directory;
        const auto config = options(directory.filePath("clips"));
        QStringList payloads;
        {
            ClipboardArchive archive(config);
            archive.add(text("first saved content"));
            archive.add(text("second saved content"));
            QTRY_COMPARE(archive.entries().size(), 2);
            for (const auto &entry : archive.entries()) payloads.append(QDir(archive.directory()).filePath(entry.id + ".clip"));
        }
        QFile index(QDir(config.value("dir").toString()).filePath("index.json"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        auto object = QJsonDocument::fromJson(index.readAll()).object(); index.close();
        if (damage == "entries-object") object["entries"] = QJsonObject{};
        else if (damage == "missing-entries") object.remove("entries");
        else {
            auto rows = object.value("entries").toArray();
            auto item = rows.last().toObject();
            if (damage == "non-object-row") rows[1] = false;
            else {
                if (damage == "invalid-id") item["id"] = "damaged";
                else item["created"] = "not a timestamp";
                rows[1] = item;
            }
            object["entries"] = rows;
        }
        const auto damaged = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QVERIFY(index.open(QIODevice::WriteOnly)); QCOMPARE(index.write(damaged), damaged.size()); index.close();
        ClipboardArchive archive(config);
        QSignalSpy errors(&archive, &ClipboardArchive::errorOccurred);
        QSignalSpy changed(&archive, &ClipboardArchive::changed);
        QTRY_VERIFY(!changed.isEmpty());
        QCOMPARE(errors.count(), 1);
        QVERIFY(archive.entries().isEmpty());
        archive.add(text("must not overwrite history"));
        archive.clear(true);
        QTRY_COMPARE(errors.count(), 3);
        for (const auto &path : payloads) QVERIFY(QFile::exists(path));
        QVERIFY(index.open(QIODevice::ReadOnly)); QCOMPARE(index.readAll(), damaged);
    }
    void migrationAndFailedCheckpointKeepOriginalData() {
        QTemporaryDir directory;
        const QString source = directory.filePath("old"), destination = directory.filePath("new");
        QString id;
        {
            ClipboardArchive archive(options(source));
            archive.add(text("keep"));
            QTRY_COMPARE(archive.entries().size(), 1);
            id = archive.entries().first().id;
            QString error;
            QVERIFY(!archive.reconfigureBlocking(options(destination), [](QString &error) { error = "failed"; return false; }, error));
            QVERIFY(QFile::exists(QDir(source).filePath(id + ".clip")));
            bool indexReadableAtCheckpoint = false;
            QVERIFY(archive.reconfigureBlocking(options(destination), [&](QString &) {
                QFile index(QDir(destination).filePath("index.json"));
                indexReadableAtCheckpoint = index.open(QIODevice::ReadOnly) && index.readAll().contains(id.toUtf8());
                return true;
            }, error));
            QVERIFY(indexReadableAtCheckpoint);
            QTRY_COMPARE(archive.directory(), destination);
        }
        QVERIFY(QFile::exists(QDir(destination).filePath(id + ".clip")));
        QVERIFY(!QFile::exists(QDir(source).filePath(id + ".clip")));
        ClipboardArchive archive(options(destination));
        QTRY_COMPARE(archive.entries().size(), 1);
        QSignalSpy ready(&archive, &ClipboardArchive::ready);
        archive.load(id);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<ClipPayload>(ready[0][1]).text, QString("keep"));
    }
    void pinnedEntriesSurviveCountAndCapacityCleanup() {
        QTemporaryDir directory;
        auto config = options(directory.filePath("clips")); config["maxItems"] = 2; config["maxMiB"] = 1;
        ClipboardArchive archive(config);
        archive.add(text("fixed"));
        QTRY_COMPARE(archive.entries().size(), 1);
        const QString id = archive.entries().first().id;
        archive.pin(id, true);
        archive.add(text("old")); archive.add(text("new"));
        QTRY_COMPARE(archive.entries().size(), 2);
        QTRY_VERIFY(archive.entries().first().pinned);
        const auto beforeOversize = archive.entries();
        QSignalSpy errors(&archive, &ClipboardArchive::errorOccurred);
        QMimeData large; large.setData("application/x-large", QByteArray(1024 * 1024, 'x'));
        archive.add(captureClipboard(large));
        QTRY_VERIFY(!errors.isEmpty());
        QCOMPARE(archive.entries().size(), beforeOversize.size());
        const auto entries = archive.entries();
        for (int i = 0; i < entries.size(); ++i) QCOMPARE(entries[i].id, beforeOversize[i].id);
        QVERIFY(std::any_of(entries.cbegin(), entries.cend(), [&](const ClipEntry &entry) { return entry.id == id; }));
        for (const auto &entry : entries) archive.pin(entry.id, true);
        QTRY_VERIFY(archive.entries().last().pinned);
        const int previousErrors = errors.count();
        archive.add(text("no free slot"));
        QTRY_VERIFY(errors.count() > previousErrors);
        QCOMPARE(archive.entries().size(), entries.size());
        for (const auto &entry : archive.entries()) QVERIFY(entry.pinned);
    }
    void previewCancellationDoesNotDiscardAnActionOrDeliverOldPreview() {
        QTemporaryDir directory;
        const auto config = options(directory.filePath("clips"));
        ClipboardArchive archive(config);
        archive.add(text("example"));
        QTRY_COMPARE(archive.entries().size(), 1);
        const auto id = archive.entries().first().id;
        using Purpose = ClipboardArchive::ReadPurpose;
        QSignalSpy ready(&archive, &ClipboardArchive::ready);
        archive.load(id, Purpose::Action);
        archive.load(id, Purpose::Preview);
        // Drain the worker while the GUI-thread deliveries are still queued.
        QString error;
        QVERIFY(!archive.reconfigureBlocking(config, [](QString &) { return false; }, error));
        archive.cancelLoads(Purpose::Preview);
        archive.load(id, Purpose::Preview);
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(qvariant_cast<Purpose>(ready[0][3]), Purpose::Action);
        QCOMPARE(qvariant_cast<Purpose>(ready[1][3]), Purpose::Preview);
    }
    void failedMigrationDoesNotReplaceExistingTargetIndex() {
        QTemporaryDir directory;
        const QString source = directory.filePath("source"), target = directory.filePath("target");
        {
            ClipboardArchive old(options(target)); old.add(text("target original")); QTRY_COMPARE(old.entries().size(), 1);
        }
        QFile index(QDir(target).filePath("index.json")); QVERIFY(index.open(QIODevice::ReadOnly)); const auto before = index.readAll(); index.close();
        ClipboardArchive archive(options(source)); archive.add(text("source original")); QTRY_COMPARE(archive.entries().size(), 1);
        QString error;
        QVERIFY(!archive.reconfigureBlocking(options(target), [](QString &message) { message = "checkpoint rejected"; return false; }, error));
        QVERIFY(index.open(QIODevice::ReadOnly)); QCOMPARE(index.readAll(), before);
        QCOMPARE(archive.directory(), source);
    }
    void lockedSourceIsNotModifiedWhenRecoveringToNewFolder() {
        QTemporaryDir directory;
        const QString source = directory.filePath("source");
        ClipboardArchive first(options(source)); first.add(text("original")); QTRY_COMPARE(first.entries().size(), 1);
        ClipboardArchive second(options(source));
        QSignalSpy errors(&second, &ClipboardArchive::errorOccurred); QTRY_VERIFY(!errors.isEmpty());
        QString error;
        QVERIFY(second.reconfigureBlocking(options(directory.filePath("other")), [](QString &) { return true; }, error));
        QSignalSpy ready(&first, &ClipboardArchive::ready); first.load(first.entries().first().id); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(qvariant_cast<ClipPayload>(ready[0][1]).text, QString("original"));
        QVERIFY(ready[0][2].toString().isEmpty());
    }
};

QTEST_MAIN(ClipboardTest)
#include "clipboard_test.moc"
