#include "clipboard/clipboard_controller.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

using namespace still;
namespace still { void setPasteTargetForTest(quintptr target); quintptr lastPasteTargetForTest(); }
static ClipPayload textPayload(const QString &text) { QMimeData mime; mime.setText(text); return captureClipboard(mime); }

class ClipboardUiTest : public QObject {
    Q_OBJECT
private slots:
    void init() { setPasteTargetForTest(1); }
    void mouseReturnCannotPasteIntoOldTarget() {
        QTemporaryDir directory;
        ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        archive.add(textPayload(QStringLiteral("保留内容，不能注入旧窗口")));
        QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, false);
        QWidget host; controller.attachHost(&host);
        setPasteTargetForTest(42); controller.prepare(); host.show();
        QVERIFY(controller.pasteDefault());
        QEvent deactivate(QEvent::WindowDeactivate); QApplication::sendEvent(&host, &deactivate);
        QVERIFY(!controller.pasteDefault());
        QSignalSpy dismissed(&controller, &ClipboardController::dismissRequested);
        controller.activate(archive.entries().first().id, ClipAction::Paste);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("保留内容，不能注入旧窗口"));
        QCOMPARE(lastPasteTargetForTest(), quintptr(0)); QCOMPARE(dismissed.count(), 0);
        QVERIFY(host.isVisible()); QGuiApplication::clipboard()->clear();
    }
    void richCopyPreservesFormatsWithoutRecordingFeedback() {
        QGuiApplication::clipboard()->clear();
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        QMimeData mime; mime.setText("Rich text"); mime.setHtml("<b>Rich text</b>"); mime.setData("text/rtf", "{\\rtf1 Rich text}");
        archive.add(captureClipboard(mime)); QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, true); QWidget host; controller.attachHost(&host);
        controller.prepare(); host.show();
        QSignalSpy dismissed(&controller, &ClipboardController::dismissRequested);
        controller.activate(archive.entries().first().id, ClipAction::Copy);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("Rich text"));
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->html(), QString("<b>Rich text</b>"));
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->data("text/rtf"), QByteArray("{\\rtf1 Rich text}"));
        QTest::qWait(200); QCOMPARE(archive.entries().size(), 1); QCOMPARE(dismissed.count(), 0); QVERIFY(host.isVisible());
        QGuiApplication::clipboard()->clear();
    }
    void plainCopyDropsRichFormatting() {
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        QMimeData mime; mime.setText("Plain content"); mime.setHtml("<b>Plain content</b>"); archive.add(captureClipboard(mime));
        QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, false); QWidget host; controller.attachHost(&host); host.show();
        controller.activate(archive.entries().first().id, ClipAction::PlainPaste);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("Plain content"));
        QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasHtml()); QGuiApplication::clipboard()->clear();
    }
    void pauseResumeAndDisabledRecording() {
        QGuiApplication::clipboard()->clear();
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        ClipboardController controller(archive, {}, true); QWidget host; controller.attachHost(&host); host.show();
        controller.setPaused(true); QVERIFY(controller.paused());
        QGuiApplication::clipboard()->setText("not recorded"); QTest::qWait(50); QVERIFY(archive.entries().isEmpty());
        controller.setPaused(false); QGuiApplication::clipboard()->setText("recorded"); QTRY_COMPARE(archive.entries().size(), 1);
        controller.configure({{"enabled", false}}); QSignalSpy settings(&controller, &ClipboardController::settingsRequested);
        controller.setPaused(false); QVERIFY(controller.paused()); QCOMPARE(settings.count(), 1);
        QGuiApplication::clipboard()->clear();
    }
    void cancelAndHostDestructionDiscardPendingAction() {
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        archive.add(textPayload("pending")); QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, false);
        auto *host = new QWidget; controller.attachHost(host); host->show();
        QSignalSpy feedback(&controller, &ClipboardController::feedback);
        controller.activate(archive.entries().first().id, ClipAction::Copy); controller.cancel();
        QTest::qWait(80); QVERIFY(feedback.isEmpty());
        controller.activate(archive.entries().first().id, ClipAction::Copy); delete host;
        QTest::qWait(80); QVERIFY(feedback.isEmpty());
        QWidget replacement; controller.attachHost(&replacement); replacement.show();
        controller.activate(archive.entries().first().id, ClipAction::Copy);
        QTRY_COMPARE(feedback.count(), 1); QGuiApplication::clipboard()->clear();
    }
    void reliablePasteDismissesHost() {
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        archive.add(textPayload("paste back")); QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, false); QWidget host; controller.attachHost(&host);
        setPasteTargetForTest(77); controller.prepare(); host.show();
        connect(&controller, &ClipboardController::dismissRequested, &host, &QWidget::hide);
        controller.activate(archive.entries().first().id, ClipAction::Paste);
        QTRY_VERIFY(!host.isVisible()); QTRY_COMPARE(lastPasteTargetForTest(), quintptr(77));
        QGuiApplication::clipboard()->clear();
    }
};
QTEST_MAIN(ClipboardUiTest)
#include "clipboard_ui_test.moc"
