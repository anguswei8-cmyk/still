#include "storage/history.h"
#include "storage/store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace still;

static QJsonObject preferences(const QString &directory, bool enabled = true) {
    return {{"dir", directory}, {"enabled", enabled}, {"maxItems", 200}, {"maxMiB", 1024}};
}

static void configure(HistoryStore &store, const QJsonObject &options, const QString &file) {
    store.reconfigure(options, [file, options](QString &error) { return writeJson(file, options, &error); });
}

static QImage image(QRgb color = qRgb(100, 150, 200)) {
    QImage result(320, 180, QImage::Format_ARGB32);
    result.fill(color);
    return result;
}

class HistoryTest : public QObject {
    Q_OBJECT
private slots:
    void savesOffThreadAndDoesNotDuplicateSameImage() {
        QTemporaryDir directory;
        HistoryStore store(preferences(directory.filePath("history")));
        QSignalSpy changed(&store, &HistoryStore::changed);
        QSignalSpy errors(&store, &HistoryStore::errorOccurred);
        const auto picture = image();
        store.remember(picture);
        store.remember(picture.copy());
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(historyFiles(directory.filePath("history")).size(), 1);
        QCOMPARE(errors.count(), 0);
    }
    void clearRunsAfterAcceptedWrites() {
        QTemporaryDir directory;
        const QString path = directory.filePath("history");
        HistoryStore store(preferences(path));
        QSignalSpy changed(&store, &HistoryStore::changed);
        store.remember(image(Qt::red));
        store.clear();
        QTRY_COMPARE(changed.count(), 2);
        QVERIFY(historyFiles(path).isEmpty());
        store.remember(image(Qt::blue));
        QTRY_COMPARE(changed.count(), 3);
        QCOMPARE(historyFiles(path).size(), 1);
        QCOMPARE(QImage(historyFiles(path).first()), image(Qt::blue));
    }
    void removeSharesWriteOrderAndRejectsOutsideFiles() {
        QTemporaryDir directory;
        const QString path = directory.filePath("history");
        const QString selected = QDir(path).filePath("selected.png");
        const QString outside = directory.filePath("manual.png");
        QVERIFY(saveImage(image(Qt::red), selected, nullptr));
        QVERIFY(saveImage(image(Qt::green), outside, nullptr));
        HistoryStore store(preferences(path));
        QSignalSpy changed(&store, &HistoryStore::changed);
        QSignalSpy errors(&store, &HistoryStore::errorOccurred);
        store.remember(image(Qt::blue));
        store.remove(selected);
        store.remove(outside);
        QTRY_COMPARE(changed.count(), 3);
        QVERIFY(!QFile::exists(selected));
        QVERIFY(QFile::exists(outside));
        QCOMPARE(historyFiles(path).size(), 1);
        QCOMPARE(errors.count(), 1);
    }
    void disabledHistoryDoesNotWrite() {
        QTemporaryDir directory;
        HistoryStore store(preferences(directory.filePath("history"), false));
        QSignalSpy changed(&store, &HistoryStore::changed);
        store.remember(image());
        QTRY_COMPARE(changed.count(), 1);
        QVERIFY(historyFiles(directory.filePath("history")).isEmpty());
    }
    void asynchronousReadsUseTheLiveDirectoryAndReleaseTheFile() {
        QTemporaryDir directory;
        const QString path = directory.filePath("history");
        const QString next = directory.filePath("new");
        HistoryStore store(preferences(path));
        QSignalSpy changed(&store, &HistoryStore::changed);
        store.remember(image());
        QTRY_COMPARE(changed.count(), 1);
        const auto before = store.list().result();
        QCOMPARE(before.entries.size(), 1);
        QVERIFY(before.bytes > 0);
        const auto file = before.entries.first().path;
        QCOMPARE(store.readImage(file).result().size(), image().size());
        QCOMPARE(store.thumbnails({file}, QSize(80, 80)).result().first().size(), QSize(80, 45));
        store.reconfigure(preferences(next), [](QString &) { return true; });
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(store.directory(), next);
        QVERIFY(store.readImage(file).result().isNull());
        const auto after = store.list().result();
        QCOMPARE(after.entries.size(), 1);
        const auto migrated = after.entries.first().path;
        QCOMPARE(store.readImage(migrated).result().size(), image().size());
        store.remove(migrated);
        QTRY_COMPARE(changed.count(), 3);
        QVERIFY(store.list().result().entries.isEmpty());
    }
    void capacityPrunesOldImagesAndRejectsOversizedNewImage() {
        QTemporaryDir directory;
        QString error;
        const auto first = addHistory(image(Qt::red), 200, &error, directory.path());
        QVERIFY(!first.isEmpty());
        const qint64 budget = QFileInfo(first).size() + 16;
        QTest::qWait(5);
        const auto second = addHistory(image(Qt::blue), 200, &error, directory.path(), budget);
        QVERIFY(!second.isEmpty());
        QCOMPARE(historyFiles(directory.path()).size(), 1);
        QVERIFY(QFile::exists(second));
        QVERIFY(!QFile::exists(first));
        QVERIFY(addHistory(image(), 200, &error, directory.path(), 1).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(QFile::exists(second));
    }
    void migrationSerializesWritesAndCommitsBeforeRemovingOriginals() {
        QTemporaryDir directory;
        const auto source = directory.filePath("old"), target = directory.filePath("new");
        const auto config = directory.filePath("settings.json");
        QDir().mkpath(source);
        QFile legacyIndex(QDir(source).filePath("history.json"));
        QVERIFY(legacyIndex.open(QIODevice::WriteOnly));
        legacyIndex.write("legacy index");
        legacyIndex.close();
        HistoryStore store(preferences(source));
        QSignalSpy saved(&store, &HistoryStore::reconfigured);
        QSignalSpy changed(&store, &HistoryStore::changed);
        store.remember(image(Qt::red));
        configure(store, preferences(target), directory.filePath("settings.json"));
        store.remember(image(Qt::blue));
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(saved[0][0].toBool());
        QTRY_COMPARE(changed.count(), 3);
        QVERIFY(historyFiles(source).isEmpty());
        QCOMPARE(historyFiles(target).size(), 2);
        QCOMPARE(readJson(config), preferences(target));
        configure(store, preferences(source), directory.filePath("settings.json"));
        QTRY_COMPARE(saved.count(), 2);
        QVERIFY(saved[1][0].toBool());
        QCOMPARE(historyFiles(source).size(), 2);
        QVERIFY(historyFiles(target).isEmpty());
        QVERIFY(legacyIndex.open(QIODevice::ReadOnly));
        QCOMPARE(legacyIndex.readAll(), QByteArray("legacy index"));
    }
    void failedSettingsCommitKeepsOriginalHistory() {
        QTemporaryDir directory;
        const auto source = directory.filePath("old"), target = directory.filePath("new");
        QVERIFY(saveImage(image(), QDir(source).filePath("original.png"), nullptr));
        // A directory cannot be atomically replaced by a settings file.
        HistoryStore store(preferences(source));
        QSignalSpy saved(&store, &HistoryStore::reconfigured);
        configure(store, preferences(target), directory.path());
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(!saved[0][0].toBool());
        QCOMPARE(QImage(QDir(source).filePath("original.png")), image());
    }
    void migrationProtectsOtherFoldersAndNameCollisions_data() {
        QTest::addColumn<QString>("markerName");
        QTest::newRow("still") << QString(".still-history");
        QTest::newRow("legacy") << QString(".pixclone-history");
    }
    void migrationProtectsOtherFoldersAndNameCollisions() {
        QFETCH(QString, markerName);
        QTemporaryDir directory;
        const auto source = directory.filePath("old"), target = directory.filePath("new");
        QVERIFY(saveImage(image(Qt::red), QDir(source).filePath("same.png"), nullptr));
        QVERIFY(saveImage(image(Qt::blue), QDir(target).filePath("same.png"), nullptr));
        HistoryStore store(preferences(source));
        QSignalSpy saved(&store, &HistoryStore::reconfigured);
        configure(store, preferences(target), directory.filePath("settings.json"));
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(!saved[0][0].toBool());
        QCOMPARE(QImage(QDir(target).filePath("same.png")), image(Qt::blue));
        QVERIFY(QFile::exists(QDir(source).filePath("same.png")));
        QFile marker(QDir(target).filePath(markerName));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();
        configure(store, preferences(target), directory.filePath("settings.json"));
        QTRY_COMPARE(saved.count(), 2);
        QVERIFY(saved[1][0].toBool());
        QCOMPARE(historyFiles(target).size(), 2);
        QCOMPARE(QImage(QDir(target).filePath("same.png")), image(Qt::blue));
        QVERIFY(historyFiles(source).isEmpty());
        QVERIFY(QFile::exists(QDir(target).filePath(".still-history")));
    }
    void migrationRejectsNestedAndRelativeDirectories() {
        QTemporaryDir directory;
        const auto source = directory.filePath("old");
        HistoryStore store(preferences(source));
        QSignalSpy saved(&store, &HistoryStore::reconfigured);
        configure(store, preferences(QDir(source).filePath("nested")), directory.filePath("settings.json"));
        QTRY_COMPARE(saved.count(), 1);
        QVERIFY(!saved[0][0].toBool());
        configure(store, preferences("relative-path"), directory.filePath("settings.json"));
        QTRY_COMPARE(saved.count(), 2);
        QVERIFY(!saved[1][0].toBool());
    }
    void shutdownFlushesAcceptedWrites() {
        QTemporaryDir directory;
        {
            HistoryStore store(preferences(directory.filePath("history")));
            store.remember(image(Qt::red));
            store.remember(image(Qt::blue));
        }
        QCOMPARE(historyFiles(directory.filePath("history")).size(), 2);
    }
    void largeImageSaveTiming() {
        QTemporaryDir directory;
        QImage picture(2560, 1440, QImage::Format_ARGB32);
        quint32 random = 12345;
        for (int y = 0; y < picture.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(picture.scanLine(y));
            for (int x = 0; x < picture.width(); ++x) { random = random * 1664525 + 1013904223; row[x] = 0xff000000 | (random & 0xffffff); }
        }
        QElapsedTimer elapsed;
        elapsed.start();
        QString error;
        QVERIFY(!addHistory(picture, 200, &error, directory.filePath("synchronous")).isEmpty());
        const qint64 synchronous = elapsed.nsecsElapsed();
        HistoryStore store(preferences(directory.filePath("background")));
        QSignalSpy changed(&store, &HistoryStore::changed);
        bool ticked = false;
        QTimer::singleShot(0, this, [&] { ticked = true; });
        elapsed.restart();
        store.remember(picture);
        const qint64 enqueue = elapsed.nsecsElapsed();
        QTRY_VERIFY(ticked);
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(historyFiles(directory.filePath("background")).size(), 1);
        qInfo("2560x1440 PNG: synchronous GUI call %.2f ms; background enqueue %.3f ms", synchronous / 1000000.0, enqueue / 1000000.0);
    }
    void excessiveQueuedImageIsRejectedWithoutRetainingIt() {
        QTemporaryDir directory;
        HistoryStore store(preferences(directory.filePath("history")));
        QSignalSpy errors(&store, &HistoryStore::errorOccurred);
        QImage picture(8192, 9216, QImage::Format_ARGB32);
        QVERIFY(!picture.isNull());
        store.remember(picture);
        QCOMPARE(errors.count(), 1);
        QVERIFY(historyFiles(directory.filePath("history")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(HistoryTest)
#include "history_test.moc"
