#include "clipboard/clipboard_browse.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace still;

static ClipPayload text(const QString &value) { QMimeData mime; mime.setText(value); return captureClipboard(mime); }
static ClipPayload picture(const QColor &color) {
    QImage image(30, 20, QImage::Format_ARGB32); image.fill(color);
    QMimeData mime; mime.setImageData(image); return captureClipboard(mime);
}

class ClipboardBrowseTest : public QObject {
    Q_OBJECT
private slots:
    void refreshKeepsSelectionAndDeletionSelectsNeighbor() {
        QTemporaryDir directory;
        ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        ClipboardBrowse browse(archive);
        for (const auto &value : {"first", "second", "third"}) archive.add(text(value));
        QTRY_COMPARE(browse.clips().size(), 3);
        browse.setSelectedIndex(1);
        const auto selected = browse.selectedId();
        archive.add(text("newest"));
        QTRY_COMPARE(browse.clips().size(), 4);
        QCOMPARE(browse.selectedId(), selected);
        QCOMPARE(browse.selectedIndex(), 2);
        const auto neighbor = browse.clips()[3].toMap().value("id").toString();
        archive.remove(selected);
        QTRY_COMPARE(browse.clips().size(), 3);
        QCOMPARE(browse.selectedId(), neighbor);
        archive.pin(neighbor, true);
        QTRY_VERIFY(browse.selected().value("pinned").toBool());
        browse.setFilter("pinned"); QCOMPARE(browse.clips().size(), 1);
        browse.setSearch("no match"); QVERIFY(browse.clips().isEmpty());
        QCOMPARE(browse.selectedIndex(), -1); QVERIFY(browse.selectedId().isEmpty());
        browse.setSearch(" FIRST "); QCOMPARE(browse.clips().size(), 1);
        QCOMPARE(browse.selectedId(), neighbor);
    }
    void previewRequiresVisibleContentAndEnoughSpace() {
        QTemporaryDir directory;
        ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        ClipboardBrowse browse(archive);
        archive.add(picture(Qt::red)); QTRY_COMPARE(browse.clips().size(), 1);
        QSignalSpy ready(&archive, &ClipboardArchive::ready);
        browse.setPreviewVisible(true); browse.setPreviewFits(true);
        QTest::qWait(160); QVERIFY(ready.isEmpty());
        browse.setActive(true);
        QTRY_VERIFY(!browse.previewImage().isNull());
        QCOMPARE(browse.previewImage().pixelColor(0, 0), QColor(Qt::red));
        browse.setPreviewFits(false);
        QVERIFY(browse.previewImage().isNull()); QVERIFY(browse.previewUrl().isEmpty());
        const auto reads = ready.size(); QTest::qWait(160); QCOMPARE(ready.size(), reads);
        browse.setPreviewFits(true); QTRY_VERIFY(!browse.previewImage().isNull());
        browse.setActive(false);
        QVERIFY(browse.previewImage().isNull()); QVERIFY(browse.previewUrl().isEmpty());
        QVERIFY(browse.previewVisible());
        browse.setActive(true); QTRY_VERIFY(!browse.previewImage().isNull());
        browse.setPreviewVisible(false); QVERIFY(browse.previewImage().isNull());
    }
    void changingSelectionDiscardsQueuedOldPreview() {
        QTemporaryDir directory;
        const QJsonObject options{{"dir", directory.filePath("clips")}};
        ClipboardArchive archive(options);
        ClipboardBrowse browse(archive);
        archive.add(picture(Qt::red)); archive.add(picture(Qt::blue));
        QTRY_COMPARE(browse.clips().size(), 2);
        browse.setActive(true); browse.setPreviewFits(true); browse.setPreviewVisible(true);
        QSignalSpy ready(&archive, &ClipboardArchive::ready);
        archive.load(browse.selectedId(), ClipboardArchive::ReadPurpose::Preview);
        QString error;
        QVERIFY(!archive.reconfigureBlocking(options, [](QString &) { return false; }, error));
        browse.setSelectedIndex(1);
        QTRY_VERIFY(!browse.previewImage().isNull());
        QCOMPARE(browse.previewImage().pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first()[0].toString(), browse.selectedId());
    }
    void leavingOrDestroyingBrowseDoesNotCancelExplicitAction() {
        QTemporaryDir directory;
        const QJsonObject options{{"dir", directory.filePath("clips")}};
        ClipboardArchive archive(options);
        archive.add(text("copy this")); QTRY_COMPARE(archive.entries().size(), 1);
        using Purpose = ClipboardArchive::ReadPurpose;
        for (bool destroy : {false, true}) {
            auto *browse = new ClipboardBrowse(archive);
            browse->setActive(true); browse->setPreviewFits(true); browse->setPreviewVisible(true);
            QSignalSpy ready(&archive, &ClipboardArchive::ready);
            archive.load(browse->selectedId(), Purpose::Preview);
            archive.load(browse->selectedId(), Purpose::Action);
            QString error;
            QVERIFY(!archive.reconfigureBlocking(options, [](QString &) { return false; }, error));
            if (destroy) delete browse;
            else browse->setActive(false);
            QTRY_COMPARE(ready.size(), 1);
            QCOMPARE(qvariant_cast<Purpose>(ready.first()[3]), Purpose::Action);
            if (!destroy) { QVERIFY(browse->previewImage().isNull()); delete browse; }
        }
    }
};
QTEST_MAIN(ClipboardBrowseTest)
#include "clipboard_browse_test.moc"
