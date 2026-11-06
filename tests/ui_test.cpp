#include "imaging/editor.h"
#include "capture/overlay.h"
#include "imaging/pin.h"
#include "ui/toolicons.h"
#include "imaging/ocr.h"
#include "tracked_image.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QMessageBox>
#include <QDialog>
#include <QPushButton>
#include <QTimer>
#include <QDir>
#include <QHBoxLayout>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QAccessible>
#include <QScopeGuard>

extern std::atomic_bool ocrBlocked;
extern std::atomic_int ocrStarted;

using namespace still;

static QImage whiteImage(int width = 300) {
    QImage image(width, 150, QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

class UiTest : public QObject {
    Q_OBJECT
private:
    Pin *pin(const QImage &image = whiteImage()) {
        auto *window = new Pin(image, QRect(100, 100, image.width(), image.height()), {}, "en-US");
        window->show();
        return window;
    }
    void copy(Pin *window) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(window, &event);
    }
private slots:
    void init() { QApplication::clipboard()->clear(); }
    void cleanup() {
        for (auto *widget : QApplication::topLevelWidgets()) widget->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void pinSelectsAndCopiesText() {
        auto *window = pin();
        QTest::qWait(60);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(window, QPoint(80, 20));
        QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(80, 20));
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello Qt"));
    }
    void doubleClickPinConfirmsCloseAndCanCancel() {
        QPointer<Pin> window = pin();
        window->configure(true, true);
        QSignalSpy disabled(window, &Pin::doubleClickClosePromptDisabled);
        QTest::mouseDClick(window, Qt::RightButton, {}, QPoint(20, 20));
        QVERIFY(window && window->isVisible());
        bool prompted = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *box = window->findChild<QDialog *>("PinClosePrompt"); box && box->isVisible()) {
                prompted = true;
                auto *skip = box->findChild<QCheckBox *>("PinCloseSkip"); QVERIFY(skip); QVERIFY(!skip->isChecked());
                skip->setChecked(true);
                box->findChild<QPushButton *>("PinCloseCancel")->click();
            }
        });
        dismiss.start(10);
        QTest::mouseDClick(window, Qt::LeftButton, {}, QPoint(20, 20));
        QTRY_VERIFY(prompted); QVERIFY(window && window->isVisible()); QCOMPARE(disabled.count(), 0);
        dismiss.stop(); prompted = false;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTimer::singleShot(0, this, [&] {
            if (auto *box = window->findChild<QDialog *>("PinClosePrompt")) {
                prompted = true; box->findChild<QPushButton *>("PinCloseConfirm")->click();
            }
        });
        QTest::mouseDClick(window, Qt::LeftButton, {}, QPoint(20, 20));
        QTRY_VERIFY(prompted); QTRY_VERIFY(window.isNull()); QCOMPARE(disabled.count(), 0);
    }
    void pinClosePromptDoesNotMakeTheApplicationModal() {
        auto *window = pin();
        QTest::mouseDClick(window, Qt::LeftButton, {}, QPoint(20, 20));
        auto *prompt = window->findChild<QWidget *>("PinClosePrompt");
        QVERIFY(prompt && prompt->isVisible());
        QCOMPARE(prompt->windowModality(), Qt::NonModal);
        QVERIFY(!QApplication::activeModalWidget());
    }
    void closingPinWhileConfirmationIsOpenReleasesPrompt() {
        QPointer<Pin> window = pin();
        QTest::mouseDClick(window, Qt::LeftButton, {}, QPoint(20, 20));
        QPointer<QDialog> prompt = window->findChild<QDialog *>("PinClosePrompt");
        QVERIFY(prompt && prompt->isVisible());
        window->close(); QTRY_VERIFY(window.isNull()); QVERIFY(prompt.isNull());
    }
    void pinSelectionSurvivesScaling() {
        auto *window = pin();
        QTest::qWait(60);
        window->resize(606, 328);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(35, 35));
        QTest::mouseMove(window, QPoint(160, 40));
        QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(160, 40));
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello Qt"));
    }
    void pinCopiesImageWithoutTextSelection() {
        auto *window = pin();
        copy(window);
        QCOMPARE(QApplication::clipboard()->image(), whiteImage());
    }
    void pinSelectAllPreservesLineBreaks() {
        auto *window = pin();
        QTest::qWait(60);
        QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
        QApplication::sendEvent(window, &select);
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello Qt\nNext"));
    }
    void staleRecognitionCannotReplaceEditedImage() {
        auto *window = pin(whiteImage(200));
        QTest::qWait(10);
        window->setImage(whiteImage(300));
        QTest::qWait(170);
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(20, 45));
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello"));
    }
    void closingPinDuringRecognitionDoesNotCrash() {
        auto *window = pin(whiteImage(200));
        QPointer<Pin> guard = window;
        QTest::qWait(5);
        window->close();
        QTRY_VERIFY(guard.isNull());
        QTest::qWait(150);
    }
    void pendingOcrImagesAreReleasedWhenPinsClose() {
        ocrBlocked = true;
        const auto unblock = qScopeGuard([] { ocrBlocked = false; });
        const int started = ocrStarted;
        new OcrJob(whiteImage(), "en-US", this);
        new OcrJob(whiteImage(), "en-US", this);
        QTRY_COMPARE(ocrStarted.load(), started + 2);
        for (int i = 0; i < 24; ++i) {
            auto *window = pin(trackedImage());
            QCoreApplication::processEvents();
            window->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QCOMPARE(liveTrackedImages.load(), 0);
    }
    void replacingImageKeepsOnlyLatestPendingOcr() {
        // Let earlier tests drain before occupying both native OCR slots.
        QTest::qWait(150);
        ocrBlocked = true;
        const auto unblock = qScopeGuard([] { ocrBlocked = false; });
        const int started = ocrStarted;
        new OcrJob(whiteImage(), "en-US", this);
        new OcrJob(whiteImage(), "en-US", this);
        QTRY_COMPARE(ocrStarted.load(), started + 2);
        auto *window = pin(trackedImage());
        QCoreApplication::processEvents();
        for (int i = 0; i < 24; ++i) window->setImage(trackedImage());
        QCOMPARE(liveTrackedImages.load(), 1);
        QCOMPARE(window->findChildren<OcrJob *>().size(), 1);
        window->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(liveTrackedImages.load(), 0);
    }
    void closingActiveJobsDoesNotStallWaitingRecognition() {
        QTest::qWait(150);
        ocrBlocked = true;
        const auto unblock = qScopeGuard([] { ocrBlocked = false; });
        const int started = ocrStarted;
        auto *first = new OcrJob(trackedImage(), "en-US", this);
        auto *second = new OcrJob(trackedImage(), "en-US", this);
        QTRY_COMPARE(ocrStarted.load(), started + 2);
        auto *waiting = new OcrJob(whiteImage(), "en-US", this);
        QSignalSpy finished(waiting, &OcrJob::finished);
        delete first;
        delete second;
        QCOMPARE(liveTrackedImages.load(), 2);
        ocrBlocked = false;
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(liveTrackedImages.load(), 0);
    }
    void repeatedEditorOcrKeepsOnlyLatestRequest() {
        auto *editor = new Editor(whiteImage(200), {}, "en-US");
        QAction *ocr = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("复制文字")) ocr = action;
        QVERIFY(ocr);
        for (int i = 0; i < 24; ++i) ocr->trigger();
        QCOMPARE(editor->findChildren<OcrJob *>().size(), 1);
    }
    void overlayReturnsDevicePixelsAndGlobalGeometry() {
        auto *overlay = new Overlay({whiteImage(600), QRect(-300, 0, 300, 75), {}}, "copy", false);
        QSignalSpy result(overlay, &Overlay::accepted);
        overlay->show();
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(overlay, QPoint(120, 60));
        QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(120, 60));
        QCOMPARE(result.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(result[0][0]).size(), QSize(200, 80));
        QCOMPARE(result[0][1].toRect(), QRect(-280, 20, 100, 40));
    }
    void overlayCancellationSignalsOnce() {
        auto *overlay = new Overlay({whiteImage(), QRect(0, 0, 300, 150), {}}, "annotate", false);
        QSignalSpy cancelled(overlay, &Overlay::cancelled);
        overlay->show();
        QTest::keyClick(overlay, Qt::Key_Escape);
        QCOMPARE(cancelled.count(), 1);
    }
    void canvasDrawingUndoAndRedo() {
        Canvas canvas(whiteImage());
        canvas.resize(600, 400);
        canvas.show();
        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&canvas, QPoint(300, 250));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(300, 250));
        QVERIFY(canvas.document.canUndo());
        QVERIFY(canvas.rendered() != whiteImage());
        canvas.undo();
        QCOMPARE(canvas.rendered(), whiteImage());
        canvas.redo();
        QVERIFY(canvas.rendered() != whiteImage());
    }
    void editorCopyExportsRenderedImage() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {{20, 20}, {100, 100}}, Qt::red, 3, {}});
        QAction *copyAction = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("复制图片")) copyAction = action;
        QVERIFY(copyAction);
        copyAction->trigger();
        QCOMPARE(QApplication::clipboard()->image(), canvas->rendered());
    }
    void annotationIconButtonsKeepSelectionAndShortcuts() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->show();
        auto *toolbar = editor->findChild<QToolBar *>();
        auto *canvas = editor->findChild<Canvas *>();
        QVERIFY(toolbar);
        QVERIFY(canvas);
        QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
        int annotations = 0;
        QAction *arrow = nullptr;
        for (QAction *action : toolbar->actions()) {
            if (action->isSeparator()) continue;
            if (auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action))) {
                QVERIFY(!action->icon().isNull());
                QVERIFY(!button->toolTip().isEmpty());
                QVERIFY(!button->accessibleName().isEmpty());
                auto *accessible = QAccessible::queryAccessibleInterface(button);
                QVERIFY(accessible);
                QCOMPARE(accessible->text(QAccessible::Name), action->text());
            }
            if (action->objectName().startsWith("annotationTool-")) ++annotations;
            if (action->text() == QStringLiteral("箭头")) arrow = action;
        }
        QCOMPARE(annotations, 11);
        QVERIFY(arrow);
        QTest::mouseClick(toolbar->widgetForAction(arrow), Qt::LeftButton);
        QCOMPARE(canvas->tool, Tool::Arrow);
        QVERIFY(arrow->isChecked());
        editor->activateWindow();
        QTest::qWait(20);
        QTest::keyClick(editor, Qt::Key_R);
        QCOMPARE(canvas->tool, Tool::Rectangle);
        QVERIFY(!arrow->isChecked());
    }
    void captureIconButtonsKeepTheirActions() {
        auto *overlay = new Overlay({whiteImage(900), QRect(0, 0, 900, 150), {}}, "annotate", false);
        overlay->show();
        QSignalSpy result(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(overlay, QPoint(700, 80));
        QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(700, 80));
        const QStringList actions{"edit", "copy", "save", "pin", "ocr", "scroll"};
        for (const auto &action : actions) {
            auto *button = overlay->findChild<QToolButton *>("captureAction-" + action);
            QVERIFY(button);
            QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->accessibleName().isEmpty());
            QVERIFY(button->toolTip().contains(button->accessibleName()));
            QTest::mouseClick(button, Qt::LeftButton);
            QCOMPARE(result.last()[2].toString(), action);
        }
        QCOMPARE(result.count(), actions.size());
    }
    void toolbarIconsRenderAtBothScales() {
        for (int glyph = 0; glyph <= int(ToolIcon::More); ++glyph) {
            for (int size : {24, 48}) {
                const QImage image = toolIcon(ToolIcon(glyph)).pixmap(size, size).toImage();
                QVERIFY(!image.isNull());
                bool visible = false;
                for (int y = 0; y < image.height() && !visible; ++y)
                    for (int x = 0; x < image.width(); ++x) if (image.pixelColor(x, y).alpha()) { visible = true; break; }
                QVERIFY2(visible, qPrintable(QStringLiteral("Missing toolbar glyph %1").arg(glyph)));
            }
        }
    }
    void narrowEditorKeepsOverflowToolsDiscoverable() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->resize(600, 450);
        editor->show();
        QTest::qWait(30);
        auto *toolbar = editor->findChild<QToolBar *>();
        QVERIFY(toolbar);
        QToolButton *overflow = nullptr;
        for (auto *button : toolbar->findChildren<QToolButton *>()) if (!button->defaultAction()) overflow = button;
        QVERIFY(overflow);
        QVERIFY(overflow->isVisible());
        QVERIFY(!overflow->icon().isNull());
        QCOMPARE(overflow->accessibleName(), QStringLiteral("更多工具"));
    }
    void toolbarPreviewSnapshots() {
        const QString directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.impeccable/review");
        QVERIFY(QDir().mkpath(directory));
        const QString tag = qEnvironmentVariable("PIXCLONE_TOOLBAR_SNAPSHOT_TAG", "current");
        QImage image(900, 620, QImage::Format_ARGB32);
        image.fill(QColor("#f4f6fa"));
        QPainter painter(&image);
        painter.fillRect(QRect(90, 90, 720, 440), Qt::white);
        painter.setPen(QColor("#25364c"));
        QFont font = painter.font();
        font.setPixelSize(28);
        painter.setFont(font);
        painter.drawText(130, 170, QStringLiteral("截图工具栏预览"));
        font.setPixelSize(15);
        painter.setFont(font);
        painter.setPen(QColor("#596a7f"));
        painter.drawText(130, 210, QStringLiteral("合成测试图片，用于验证工具按钮的显示和操作。"));
        painter.end();
        auto *editor = new Editor(image, {}, "en-US");
        editor->show();
        QTest::qWait(40);
        auto *toolbar = editor->findChild<QToolBar *>();
        QVERIFY(toolbar);
        QVERIFY(toolbar->grab().save(directory + "/editor-toolbar-" + tag + ".png"));
        QVERIFY(editor->grab().save(directory + "/editor-window-" + tag + ".png"));
        editor->resize(820, 620);
        QTest::qWait(40);
        QVERIFY(toolbar->grab().save(directory + "/editor-toolbar-compact-" + tag + ".png"));
        auto *overlay = new Overlay({image, QRect(0, 0, 900, 620), {}}, "annotate", false);
        overlay->show();
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 240));
        QTest::mouseMove(overlay, QPoint(760, 480));
        QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(760, 480));
        auto *layout = overlay->findChild<QHBoxLayout *>();
        QVERIFY(layout);
        QTest::qWait(40);
        QVERIFY(layout->parentWidget()->grab().save(directory + "/capture-toolbar-" + tag + ".png"));
        QVERIFY(overlay->grab().save(directory + "/capture-window-" + tag + ".png"));
    }
};

QTEST_MAIN(UiTest)
#include "ui_test.moc"
