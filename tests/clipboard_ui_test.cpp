#include "clipboard/clipboard_controller.h"
#include "imaging/pin_state.h"
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
    void textPinCarriesPlainTextAndHtml_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QUrl>("url");
        const QString raw = QStringLiteral("  <b>原样文字</b>\t😀\r\n第二行  ");
        QTest::newRow("plain") << raw << QString() << raw << QUrl();
        QTest::newRow("rich-plain-payload") << raw << QStringLiteral("<b>其他格式内容</b>\r\n<a href=\"https://example.com\">网页</a>") << raw << QUrl();
        QTest::newRow("html-only-fallback") << QString() << QStringLiteral("<b>富文本内容</b>") << QStringLiteral("富文本内容") << QUrl();
        QTest::newRow("link") << QStringLiteral("https://example.com/路径") << QString() << QStringLiteral("https://example.com/路径") << QUrl("https://example.com/路径");
    }
    void textPinCarriesPlainTextAndHtml() {
        QFETCH(QString, text); QFETCH(QString, html); QFETCH(QString, expected); QFETCH(QUrl, url);
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        QMimeData mime;
        if (!text.isEmpty()) mime.setText(text);
        if (!html.isEmpty()) mime.setHtml(html);
        if (!url.isEmpty()) mime.setUrls({url});
        archive.add(captureClipboard(mime)); QTRY_COMPARE(archive.entries().size(), 1);
        if (!url.isEmpty()) QCOMPARE(archive.entries().first().kind, QString("link"));
        ClipboardController controller(archive, {}, false); QWidget host; controller.attachHost(&host); host.show();
        QSignalSpy pinned(&controller, &ClipboardController::pinText), image(&controller, &ClipboardController::pinImage);
        QSignalSpy feedback(&controller, &ClipboardController::feedback), dismissed(&controller, &ClipboardController::dismissRequested);
        QGuiApplication::clipboard()->setText("clipboard stays unchanged");
        controller.activate(archive.entries().first().id, ClipAction::PinText);
        QTRY_COMPARE(pinned.count(), 1); QCOMPARE(pinned.first().first().toString(), expected);
        QCOMPARE(pinned.first().at(1).toString(), html);
        QVERIFY(image.isEmpty()); QVERIFY(feedback.isEmpty()); QVERIFY(dismissed.isEmpty()); QVERIFY(host.isVisible());
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("clipboard stays unchanged"));
        QGuiApplication::clipboard()->clear();
    }
    void textPinRejectsEmptyAndOversizedCombinedPayloads() {
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        ClipboardController controller(archive, {}, false); QWidget host; controller.attachHost(&host);
        QSignalSpy pinned(&controller, &ClipboardController::pinText), feedback(&controller, &ClipboardController::feedback);
        const auto dispatch = [&](const QString &text, const QString &html = {}) {
            controller.activate("text-pin-boundary", ClipAction::PinText);
            ClipPayload payload; payload.text = text;
            if (!html.isEmpty()) payload.formats.append({"text/html", html.toUtf8()});
            archive.ready("text-pin-boundary", payload, {}, ClipboardArchive::ReadPurpose::Action);
        };
        dispatch({}); QCOMPARE(feedback.count(), 1); QVERIFY(pinned.isEmpty());
        dispatch(QString(maximumTextPinBytes + 1, QChar('x'))); QCOMPARE(feedback.count(), 2); QVERIFY(pinned.isEmpty());
        dispatch(QString(maximumTextPinBytes / 3 + 1, QChar(0x6587))); QCOMPARE(feedback.count(), 3); QVERIFY(pinned.isEmpty());
        dispatch(QString(maximumTextPinBytes, QChar('x'))); QCOMPARE(pinned.count(), 1); QCOMPARE(feedback.count(), 3);
        QCOMPARE(pinned.first().first().toString().size(), maximumTextPinBytes);
        dispatch({}, "<b>HTML without plain fallback</b>"); QCOMPARE(pinned.count(), 1); QCOMPARE(feedback.count(), 4);
        dispatch("x", QString(maximumTextPinBytes, QChar('h'))); QCOMPARE(pinned.count(), 1); QCOMPARE(feedback.count(), 5);
        dispatch("x", QString(maximumTextPinBytes / 3 + 1, QChar(0x6587))); QCOMPARE(pinned.count(), 1); QCOMPARE(feedback.count(), 6);
        const QString html(maximumTextPinBytes - 1, QChar('h'));
        dispatch("x", html); QCOMPARE(pinned.count(), 2); QCOMPARE(feedback.count(), 6);
        QCOMPARE(pinned.last().at(1).toString(), html);
    }
    void imageActionsKeepImageDispatch_data() {
        QTest::addColumn<ClipAction>("action");
        QTest::newRow("edit") << ClipAction::EditImage;
        QTest::newRow("pin") << ClipAction::PinImage;
        QTest::newRow("save") << ClipAction::SaveImage;
    }
    void imageActionsKeepImageDispatch() {
        QFETCH(ClipAction, action);
        QTemporaryDir directory; ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        QImage source(32, 24, QImage::Format_ARGB32); source.fill(Qt::green);
        QMimeData mime; mime.setImageData(source); archive.add(captureClipboard(mime)); QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, false); QWidget host; controller.attachHost(&host);
        QSignalSpy edited(&controller, &ClipboardController::editImage), pinned(&controller, &ClipboardController::pinImage);
        QSignalSpy saved(&controller, &ClipboardController::saveImage), text(&controller, &ClipboardController::pinText);
        auto &expected = action == ClipAction::EditImage ? edited : action == ClipAction::PinImage ? pinned : saved;
        controller.activate(archive.entries().first().id, action);
        QTRY_COMPARE(expected.count(), 1); QCOMPARE(qvariant_cast<QImage>(expected.first().first()), source);
        QCOMPARE(edited.count() + pinned.count() + saved.count(), 1); QVERIFY(text.isEmpty());
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
    void recordingCanBeEnabledAfterStartingDisabled() {
        QGuiApplication::clipboard()->clear();
        QTemporaryDir directory;
        const QJsonObject options{{"dir", directory.filePath("clips")}, {"enabled", false}};
        ClipboardArchive archive(options);
        ClipboardController controller(archive, options, true);
        QGuiApplication::clipboard()->setText("not recorded while disabled");
        QTest::qWait(50); QVERIFY(archive.entries().isEmpty());
        controller.configure({{"enabled", true}});
        QGuiApplication::clipboard()->setText("recorded after enabling");
        QTRY_COMPARE(archive.entries().size(), 1);
        QCOMPARE(archive.entries().first().preview, QString("recorded after enabling"));
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
