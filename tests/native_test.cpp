#include "platform/platform.h"
#include "clipboard/clipboard_controller.h"
#include "capture/scroll_session.h"
#include "capture/scroll_capture_ui.h"
#include "ui/i18n.h"
#include <QPainter>
#include <QPixmap>
#include <QTest>
#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QMimeData>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QLabel>
#include <QPushButton>
#include <QAbstractButton>
#include <QFontDatabase>
#include <QOperatingSystemVersion>
#include <QDir>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace still;

class NativeTest : public QObject {
    Q_OBJECT
private slots:
    void scrollingCaptureWaitsThenJoinsWheelScrolledContent_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme");
        QTest::newRow("zh-light") << QString("zh_CN") << QString("light");
        QTest::newRow("en-light") << QString("en") << QString("light");
        QTest::newRow("zh-dark") << QString("zh_CN") << QString("dark");
        QTest::newRow("en-dark") << QString("en") << QString("dark");
    }
    void scrollingCaptureWaitsThenJoinsWheelScrolledContent() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Desktop capture requires native Windows");
#ifdef Q_OS_WIN
        if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows10_2004) QSKIP("Capture exclusion requires Windows 10 2004 or newer");
#endif
        QFETCH(QString, language); QFETCH(QString, theme);
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QApplication::setPalette(QGuiApplication::palette());
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, "Microsoft YaHei UI");
        class Page : public QWidget {
            QImage image_;
        public:
            Page() : image_(640, 1800, QImage::Format_RGB32) {
                for (int y = 0; y < image_.height(); ++y) for (int x = 0; x < image_.width(); ++x) {
                    const quint32 value = quint32(y) * 2654435761u ^ quint32(x) * 2246822519u;
                    image_.setPixel(x, y, qRgb(value & 255, (value >> 8) & 255, (value >> 16) & 255));
                }
                QPainter painter(&image_);
                painter.fillRect(0, 0, image_.width(), 64, QColor("#e3e9df"));
                painter.setPen(QColor("#29322c"));
                painter.drawText(QRect(24, 0, 600, 64), Qt::AlignVCenter, "Generated scrolling test image");
                setFixedSize(image_.size());
            }
        protected:
            void paintEvent(QPaintEvent *) override { QPainter painter(this); painter.drawImage(0, 0, image_); }
        };
        QScrollArea area; area.setWidget(new Page);
        area.setWindowFlag(Qt::WindowStaysOnTopHint);
        QScreen *screen = QGuiApplication::primaryScreen(); QVERIFY(screen);
        area.resize(qMin(800, screen->availableGeometry().width() - 60), qMin(640, screen->availableGeometry().height() - 80));
        area.move(screen->availableGeometry().topLeft() + QPoint(20, 20)); area.show(); area.raise(); area.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&area)); QTest::qWait(250);
        const QRect region(area.viewport()->mapToGlobal(QPoint(6, 6)),
                           QSize(qMin(494, area.viewport()->width() - 190), area.viewport()->height() - 74));
        QImage latest;
        const auto capture = [&](const QRect &bounds) {
            return cropFrame(screen->grabWindow(0).toImage(), screen->size(), bounds.translated(-screen->geometry().topLeft()));
        };
        const QImage first = capture(region); QVERIFY(!first.isNull());
        // Never validate or save a capture of another window that covered the fixture.
        QCOMPARE(first.pixelColor(10, 10), QColor("#e3e9df"));
        ScrollCaptureUi hud(region, screen->availableGeometry());
        QVERIFY(hud.prepare()); hud.setImage(first);
        hud.move(region.right() - hud.width() - 12, region.top() + 12); hud.show(); QTest::qWait(150);
        auto *selection = hud.findChild<QWidget *>("ScrollSelectionFrame"); QVERIFY(selection); QVERIFY(selection->isVisible());
        auto *preview = hud.findChild<QLabel *>("ScrollImagePreview"); QVERIFY(preview); QVERIFY(!preview->pixmap().isNull());
        auto *previewArea = hud.findChild<QScrollArea *>("ScrollPreviewArea"); QVERIFY(previewArea);
        auto *previewBar = previewArea->verticalScrollBar();
        QVERIFY(!hud.windowFlags().testFlag(Qt::WindowTransparentForInput));
        auto *finish = hud.findChild<QAbstractButton *>("ScrollFinish"); QVERIFY(finish);
        auto *toolbar = hud.findChild<QWidget *>("CaptureTools"); QVERIFY(toolbar);
        const auto toolbarPixels = toolbar->grab().toImage();
        QCOMPARE(toolbarPixels.pixelColor(qRound(4 * toolbar->devicePixelRatioF()), qRound(toolbar->height() / 2.0 * toolbar->devicePixelRatioF())), toolbar->palette().color(QPalette::Base));
        toolbar->move(region.left() + 8, region.bottom() - toolbar->height() - 12);
        QTest::qWait(100);
        QVERIFY(!hud.findChild<QLabel *>("ScrollStatus")); QVERIFY(hud.findChild<QLabel *>("ScrollDimensions"));
        const QImage initialPreview = preview->pixmap().toImage();
        const int previewWidth = preview->width();
        QVERIFY(!hud.isActiveWindow()); QVERIFY(hud.geometry().intersects(region));
        // Both visible windows overlap the capture; desktop pixels must still be unchanged.
        QCOMPARE(capture(region).convertToFormat(QImage::Format_RGB32), first.convertToFormat(QImage::Format_RGB32));
        ScrollSession session([&](const QRect &bounds) { latest = capture(bounds); return latest; });
        QSignalSpy finished(&session, &ScrollSession::finished);
        QSignalSpy progress(&session, &ScrollSession::progress);
        connect(&session, &ScrollSession::progress, &hud, [&](const QSize &, int) { hud.setImage(session.image()); });
        connect(&hud, &ScrollCaptureUi::finishRequested, &session, &ScrollSession::stop);
        QVERIFY(session.start(region, first, {200, 3000, 80}));
        QTest::qWait(500); QVERIFY(session.active()); QCOMPARE(finished.count(), 0);
        const QPoint position = area.viewport()->rect().center();
        for (int step = 0; step < 6; ++step) {
            const int before = progress.count();
            QWheelEvent wheel(position, area.viewport()->mapToGlobal(position), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(area.viewport(), &wheel);
            QTRY_VERIFY_WITH_TIMEOUT(progress.count() > before, 5000);
            QCOMPARE(preview->width(), previewWidth);
            if (step < 5) QTRY_COMPARE(previewBar->value(), previewBar->maximum());
            if (step == 4) { QVERIFY(previewBar->maximum() > 0); previewBar->setValue(0); }
            QVERIFY(selection->isVisible()); QVERIFY(hud.isVisible());
        }
        QVERIFY(area.verticalScrollBar()->value() > 0); QVERIFY(preview->pixmap().toImage() != initialPreview);
        QCOMPARE(previewBar->value(), 0); QVERIFY(previewBar->maximum() > 0);
        previewBar->setValue(previewBar->maximum());
        const QPoint previewPosition = previewArea->viewport()->rect().center();
        QWheelEvent browse(previewPosition, previewArea->viewport()->mapToGlobal(previewPosition), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(previewArea->viewport(), &browse);
        QVERIFY(previewBar->value() < previewBar->maximum());
        QVERIFY(previewArea->focusPolicy() & Qt::TabFocus);
        QTest::keyClick(previewArea, Qt::Key_PageDown);
        QCOMPARE(previewBar->value(), previewBar->maximum());
        session.stop(); QCOMPARE(finished.count(), 1);
        const auto result = qvariant_cast<QImage>(finished[0][0]);
        QVERIFY(result.height() > first.height()); QCOMPARE(result.width(), first.width());
        QCOMPARE(result.copy(0, 0, first.width(), first.height()).convertToFormat(QImage::Format_RGB32), first.convertToFormat(QImage::Format_RGB32));
        // Fractional-DPI repaints can differ in the old overlap; newly appended pixels stay exact.
        const int tailHeight = qMin(200, latest.height());
        QCOMPARE(result.copy(0, result.height() - tailHeight, latest.width(), tailHeight).convertToFormat(QImage::Format_RGB32),
                 latest.copy(0, latest.height() - tailHeight, latest.width(), tailHeight).convertToFormat(QImage::Format_RGB32));
        const int expectedHeight = first.height() + qRound(area.verticalScrollBar()->value() * screen->devicePixelRatio());
        QVERIFY(qAbs(result.height() - expectedHeight) <= 2);
        QVERIFY(finished[0][2].toString().isEmpty());
        QVERIFY(result.save(QCoreApplication::applicationDirPath() + "/scroll-native-result.png"));
#ifdef Q_OS_WIN
        // The capture is stopped. Reveal the windows only for an honest native UI review shot.
        QVERIFY(SetWindowDisplayAffinity(reinterpret_cast<HWND>(hud.winId()), WDA_NONE));
        QVERIFY(SetWindowDisplayAffinity(reinterpret_cast<HWND>(selection->winId()), WDA_NONE));
        QVERIFY(SetWindowDisplayAffinity(reinterpret_cast<HWND>(toolbar->winId()), WDA_NONE));
        // Restore production placement for the review, within this controlled test window.
        hud.move(region.right() + 12, region.top());
        toolbar->move(region.right() - toolbar->width() + 1, region.bottom() + 12);
        QTest::qWait(150);
        QString review = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.impeccable/review");
        if (qEnvironmentVariable("QT_SCALE_FACTOR") == "1.5") review += "/hidpi";
        QDir().mkpath(review);
        if (language == "zh_CN" && theme == "light") {
            QVERIFY(toolbar->grab().save(QDir(review).filePath("scroll-toolbar-detail.png")));
            previewBar->setValue(0);
            QVERIFY(hud.grab().save(QDir(review).filePath("scroll-preview-top.png")));
            previewBar->setValue(previewBar->maximum());
            QVERIFY(hud.grab().save(QDir(review).filePath("scroll-preview-bottom.png")));
        }
        const QSize reviewSize(qMin(area.viewport()->width(), region.width() + 188), area.viewport()->height());
        QVERIFY(capture(QRect(area.viewport()->mapToGlobal(QPoint()), reviewSize)).save(QDir(review).filePath("scroll-" + language + "-" + theme + ".png")));
#endif
        QSignalSpy finishRequested(&hud, &ScrollCaptureUi::finishRequested);
        QSignalSpy actions(&hud, &ScrollCaptureUi::actionRequested);
        const QStringList actionIds{"edit", "ocr", "pin", "save", "cancel", "copy"};
        for (const auto &action : actionIds) {
            auto *button = hud.findChild<QAbstractButton *>(action == "edit" ? "ScrollFinish" : "scrollAction-" + action);
            QVERIFY(button); QVERIFY(!button->toolTip().isEmpty());
            QTest::mouseClick(button, Qt::LeftButton);
            QCOMPARE(actions.last()[0].toString(), action);
        }
        QCOMPARE(finishRequested.count(), actionIds.size()); QCOMPARE(finished.count(), 1); hud.hide(); QVERIFY(!selection->isVisible()); QVERIFY(!toolbar->isVisible());
    }
    void systemOcrReturnsTextAndPositions() {
        QImage image(900, 220, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        QFont font;
        font.setPixelSize(64);
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(QRect(30, 40, 840, 140), Qt::AlignLeft | Qt::AlignVCenter, "Hello Qt 123");
        painter.end();
        const auto result = recognizeNative(image, "en-US");
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.words.isEmpty());
        const QString text = selectedText(result.words, 0, int(result.words.size()) - 1);
        QVERIFY2(text.contains("Hello", Qt::CaseInsensitive), qPrintable(text));
        QVERIFY2(text.contains("123"), qPrintable(text));
        for (const auto &word : result.words) {
            QVERIFY(!word.bounds.isEmpty());
            QVERIFY(QRectF(image.rect()).contains(word.bounds));
        }
    }
    void scrollingPreviewFitsScreenEdgeAndClosesItsSelection() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Native Windows capture exclusion test");
        if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows10_2004) QSKIP("Capture exclusion requires Windows 10 2004");
        auto *screen = QGuiApplication::primaryScreen(); QVERIFY(screen);
        ScrollCaptureUi hud(screen->geometry(), screen->availableGeometry());
        QVERIFY(hud.prepare());
        QVERIFY(screen->availableGeometry().contains(hud.geometry()));
        QImage first(200, 400, QImage::Format_RGB32); first.fill(Qt::white); hud.setImage(first); hud.show();
        auto *selection = hud.findChild<QWidget *>("ScrollSelectionFrame"); QVERIFY(selection);
        QTRY_VERIFY(selection->isVisible());
        QVERIFY(selection->geometry().contains(screen->geometry()));
        hud.close(); QVERIFY(!selection->isVisible());
    }
    void conflictingHotkeyKeepsExistingRegistration() {
        Hotkeys first;
        Hotkeys second;
        const QKeySequence original("Ctrl+Alt+Shift+F11");
        const QKeySequence blocked("Ctrl+Alt+Shift+F12");
        if (!first.bind(0, original) || !second.bind(0, blocked)) QSKIP("Test hotkeys are occupied by another application");
        QVERIFY(!first.bind(0, blocked));
        QVERIFY(!second.bind(1, original));
        first.unbind(0);
        QVERIFY(second.bind(1, original));
        QVERIFY(first.bind(0, {}));
    }
    void stagedReplacementKeepsOriginalUntilCommit() {
        Hotkeys first, competitor;
        const QKeySequence original("Ctrl+Alt+Shift+F9"), replacement("Ctrl+Alt+Shift+F10");
        if (!first.bind(0, original)) QSKIP("Test hotkey occupied");
        if (!first.stage(0, replacement)) QSKIP("Replacement test hotkey occupied");
        QVERIFY(!competitor.bind(0, original));
        QVERIFY(!competitor.bind(0, replacement));
        QCOMPARE(first.sequence(0), original);
        first.discardStaged();
        QVERIFY(competitor.bind(0, replacement));
        QVERIFY(!competitor.bind(1, original));
        competitor.unbind(0);
        QVERIFY(first.stage(0, replacement));
        first.commitStaged();
        QCOMPARE(first.sequence(0), replacement);
        QVERIFY(competitor.bind(1, original));
        QVERIFY(!competitor.bind(0, replacement));
    }
    void clipboardPanelPastesIntoPreviousWindow() {
#ifdef Q_OS_WIN
        auto *clipboard = QGuiApplication::clipboard();
        ClipPayload backup;
        if (const auto *original = clipboard->mimeData()) {
            if (excludedClipboard(*original)) QSKIP("Do not disturb protected clipboard content");
            for (const auto &format : original->formats())
                if (format.contains("FileContents") || format.contains("FileGroupDescriptor")) QSKIP("Do not materialize virtual clipboard files");
            QString error;
            const auto payload = captureClipboard(*original, &error);
            if (!error.isEmpty() || (payload.empty() && !original->formats().isEmpty())) QSKIP("Cannot safely preserve current clipboard");
            backup = payload;
        }
        const QString sample = QStringLiteral("PixClone panel paste 你好");
        const auto restore = qScopeGuard([&] {
            // Clipboard managers may briefly lock OLE; retain the backup for a bounded retry.
            for (int attempt = 0; attempt < 10; ++attempt) {
                if (clipboard->text() != sample) return;
                if (!backup.empty()) clipboard->setMimeData(restoreClipboard(backup)); else clipboard->clear();
                const auto *current = clipboard->mimeData();
                if (backup.empty() ? (!current || current->formats().isEmpty()) : clipboard->ownsClipboard()) return;
                QTest::qWait(30);
            }
            QFAIL("System clipboard remained busy while restoring its backup");
        });
        QTemporaryDir directory;
        ClipboardArchive archive({{"dir", directory.filePath("clips")}});
        QMimeData mime; mime.setText(sample); archive.add(captureClipboard(mime));
        QTRY_COMPARE(archive.entries().size(), 1);
        ClipboardController controller(archive, {}, false);
        QWidget host;
        controller.attachHost(&host);
        connect(&controller, &ClipboardController::dismissRequested, &host, &QWidget::hide);
        QLineEdit target; target.resize(600, 100); target.setWindowTitle("PixClone clipboard paste target"); target.show(); target.activateWindow(); target.setFocus();
        const auto handle = quintptr(target.winId());
        activatePasteTarget(handle);
        QDeadlineTimer deadline(qMax(1000, qEnvironmentVariableIntValue("PIXCLONE_NATIVE_PASTE_WAIT_MS")));
        while (currentPasteTarget() != handle && !deadline.hasExpired()) QTest::qWait(50);
        if (currentPasteTarget() != handle) QSKIP("OS did not allow test window activation");
        controller.prepare(); host.show(); host.raise(); host.activateWindow();
        QVERIFY(host.isVisible());
        controller.activate(archive.entries().first().id, ClipAction::Paste);
        QTRY_COMPARE(target.text(), sample);
        QVERIFY(!host.isVisible());
        const QString pasted = target.text();
        QString error;
        QVERIFY(!pasteToTarget(0, &error));
        QCOMPARE(target.text(), pasted);
#else
        QSKIP("Native paste is checked on Windows; Mac needs a trusted test app");
#endif
    }
};

QTEST_MAIN(NativeTest)
#include "native_test.moc"
