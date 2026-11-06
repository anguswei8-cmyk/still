#include "imaging/editor.h"
#include "capture/overlay.h"
#include "imaging/pin.h"
#include "imaging/text_pin.h"
#include "imaging/html_document.h"
#include "ui/toolicons.h"
#include "imaging/ocr.h"
#include "imaging/ocr_result_dialog.h"
#include <QPlainTextEdit>
#include <QTabWidget>
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
#include <QTemporaryDir>
#include <QFileDialog>
#include <QImageReader>
#include <QFileInfo>
#include <QFile>
#include <QMimeData>
#include <QLineEdit>
#include <QLabel>
#include <QStatusBar>
#include <QScreen>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextFragment>
#include <QToolTip>
#include <QHelpEvent>
#include <QDesktopServices>
#include <QColorDialog>
#include <QInputDialog>
#include <QSpinBox>
#include <QComboBox>

extern std::atomic_bool ocrBlocked;
extern std::atomic_int ocrStarted;

using namespace still;

static QImage whiteImage(int width = 300) {
    QImage image(width, 150, QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

static QImage coordinateImage(const QSize &size) {
    QImage image(size, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
    return image;
}

class HtmlResourceProbe : public QObject {
    Q_OBJECT
public:
    int resources = 0;
    int links = 0;
    Q_INVOKABLE QVariant loadResource(int, const QUrl &) { ++resources; return whiteImage(); }
public slots:
    void openUrl(const QUrl &) { ++links; }
};

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
    QPoint canvasPoint(Canvas *canvas, const QPointF &point) {
        const auto view = canvas->imageRect();
        const auto size = canvas->document.size();
        return (view.topLeft() + QPointF(point.x() * view.width() / size.width(), point.y() * view.height() / size.height())).toPoint();
    }
    void drawAnnotation(Canvas *canvas, const QPointF &from, const QPointF &to) {
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, from));
        QTest::mouseMove(canvas, canvasPoint(canvas, to));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, to));
    }
private slots:
    void init() { QApplication::clipboard()->clear(); }
    void cleanup() {
        for (auto *widget : QApplication::topLevelWidgets()) widget->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void textPinSelectsOriginalTextWithoutOcr() {
        const auto started = ocrStarted.load();
        const QString text = QStringLiteral("  <b>字面 HTML</b>\r\n\t😀  A\rB\n尾部  ");
        auto *window = new TextPin(text); window->show();
        auto *editor = window->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
        QVERIFY(editor->isReadOnly()); QVERIFY(!editor->acceptRichText());
        QVERIFY(editor->toPlainText().contains("<b>字面 HTML</b>"));
        QCOMPARE(ocrStarted.load(), started);
        const auto start = editor->toPlainText().indexOf(QStringLiteral("HTML"));
        const auto end = editor->toPlainText().indexOf(QStringLiteral("A"), start) + 1;
        auto cursor = editor->textCursor(); cursor.setPosition(start); cursor.setPosition(end, QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
        QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("HTML</b>\r\n\t😀  A"));
        QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), text);
        cursor.clearSelection(); editor->setTextCursor(cursor); QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), text);
        QSignalSpy closing(window, &PinWindow::closing); window->close(); QCOMPARE(closing.size(), 1);
        const auto state = qvariant_cast<PinState>(closing.first().first()); QCOMPARE(state.text, text); QVERIFY(state.image.isNull());
    }
    void textPinScrollsAndKeepsACompactMenu() {
        const QString text = QStringLiteral("一行可滚动的文字\n").repeated(300);
        auto *window = new TextPin(text); window->show();
        auto *editor = window->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
        QTRY_VERIFY(editor->verticalScrollBar()->maximum() > 0);
        const auto size = window->size(); const auto position = window->pos();
        editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum()); QCOMPARE(window->size(), size);
        QTest::mouseDClick(editor->viewport(), Qt::LeftButton, {}, QPoint(30, 10));
        QVERIFY(editor->textCursor().hasSelection()); QVERIFY(window->isVisible()); QCOMPARE(window->pos(), position);
        bool checked = false; QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *menu = window->findChild<QMenu *>("TextPinMenu"); if (!menu || !menu->isVisible()) return;
            timer.stop(); const auto dismiss = qScopeGuard([&] { menu->close(); });
            QStringList primary; for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text());
            QCOMPARE(primary, QStringList({QStringLiteral("复制"), QStringLiteral("全选"), QStringLiteral("更多操作"), QStringLiteral("关闭")}));
            menu->actions()[1]->trigger(); menu->actions()[0]->trigger(); QCOMPARE(QApplication::clipboard()->text(), text); checked = true;
        });
        timer.start(10); QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(15, 15), window->mapToGlobal(QPoint(15, 15)));
        QApplication::sendEvent(window, &event); QVERIFY(checked);
        QTest::keyClick(editor, Qt::Key_Escape); QTRY_VERIFY(!window->isVisible());
    }
    void textPinSelectionPreservesUnicodeAndNulls() {
        const QString text = QStringLiteral("\ufeff A\u00a0B\u2028C\u2029D\0E😀Z  \r\n");
        auto *window = new TextPin(text); window->show();
        auto *editor = window->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
        const auto displayed = editor->toPlainText();
        auto cursor = editor->textCursor(); cursor.setPosition(displayed.indexOf('B'));
        cursor.setPosition(displayed.indexOf('Z') + 1, QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
        QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("B\u2028C\u2029D\0E😀Z"));
        QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), text);
    }
    void htmlPinPreservesFormattingAndCopiesVisibleText() {
        const auto started = ocrStarted.load();
        const QString html = QStringLiteral("<h2>排版标题</h2><p>这是<b>强调 😀</b>和<i>斜体</i>。</p><ul><li>第一项</li><li>第二项</li></ul><table border='1'><tr><td>甲</td><td>乙</td></tr></table><pre>  a\t b\n  c</pre>");
        auto *window = new TextPin("Original clipboard fallback", html); window->show();
        auto *editor = window->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor); QVERIFY(editor->isReadOnly());
        auto cursor = editor->document()->find(QStringLiteral("强调 😀")); QVERIFY(!cursor.isNull());
        QVERIFY(cursor.charFormat().fontWeight() >= QFont::Bold); editor->setTextCursor(cursor);
        QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier); QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("强调 😀"));
        cursor = editor->document()->find(QStringLiteral("斜体")); QVERIFY(cursor.charFormat().fontItalic());
        QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), editor->toPlainText()); QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
        QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("甲"))); QVERIFY(!QApplication::clipboard()->text().contains("<b>"));
        QCOMPARE(window->state().text, QString("Original clipboard fallback")); QCOMPARE(window->state().html, html);
        QCOMPARE(ocrStarted.load(), started);
    }
    void htmlPinBlocksResourcesLinksAndRichTooltips() {
        QTemporaryDir directory; const QString image = directory.filePath("private.png"); QVERIFY(whiteImage().save(image));
        const QString style = directory.filePath("private.css"); QFile css(style); QVERIFY(css.open(QIODevice::WriteOnly));
        css.write("@import url(probe://local-file-read); p { color:#030407; }"); css.close();
        const QString html = QString("<html><head><link type='text/css' href='%1'><style>@import url('https://example.invalid/import.css');</style></head><body background='%2'>"
            "<p style='background-image:url(%2)' title=\"&lt;img src='%2'&gt;\">正常正文</p>"
            "<img src='%2'><img src='private.png'><img src='https://example.invalid/image.png'><img src='qrc:/brand/still.png'>"
            "<img src='data:image/png;base64,abcd'><img align='left' src='%2'><script>never run</script>"
            "<a href='https://example.invalid/'>链接</a><a href='file:///private/secret'>本地</a><a href='javascript:alert(1)'>脚本</a></body></html>")
            .arg(QUrl::fromLocalFile(style).toString(), QUrl::fromLocalFile(image).toString());
        int globalLoads = 0, instanceLoads = 0;
        const auto previous = QTextDocument::defaultResourceProvider();
        QTextDocument::setDefaultResourceProvider([&](const QUrl &) { ++globalLoads; return QVariant(whiteImage()); });
        HtmlResourceProbe probe;
        for (const auto &scheme : {"https", "file", "javascript"}) QDesktopServices::setUrlHandler(scheme, &probe, "openUrl");
        const auto reset = qScopeGuard([&] {
            QTextDocument::setDefaultResourceProvider(previous);
            for (const auto &scheme : {"https", "file", "javascript"}) QDesktopServices::unsetUrlHandler(scheme);
        });
        SafeHtmlDocument document(html, &probe);
        document.setResourceProvider([&](const QUrl &) { ++instanceLoads; return QVariant(whiteImage()); });
        QCOMPARE(document.documentLayout()->handlerForObject(QTextFormat::ImageObject), static_cast<QTextObjectInterface *>(&document));
        QCOMPARE(document.intrinsicSize(&document, 0, QTextImageFormat()), QSizeF(0, 0));
        for (const auto &url : {QUrl::fromLocalFile(image), QUrl("private.png"), QUrl("https://example.invalid/"), QUrl("qrc:/brand/still.png")})
            for (int type : {int(QTextDocument::ImageResource), int(QTextDocument::StyleSheetResource)}) {
                const auto resource = document.resource(type, url); QVERIFY(resource.isValid()); QVERIFY(resource.toByteArray().isEmpty());
            }
        auto *window = new TextPin("fallback", html); window->show();
        auto *editor = window->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
        QVERIFY(editor->toPlainText().contains(QStringLiteral("正常正文"))); QVERIFY(!editor->toPlainText().contains("never run"));
        QVERIFY(!editor->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse)); QVERIFY(!editor->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
        for (auto block = editor->document()->begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto format = it.fragment().charFormat(); QVERIFY(!format.isImageFormat()); QVERIFY(format.toolTip().isEmpty()); QVERIFY(!format.isAnchor()); QVERIFY(format.anchorHref().isEmpty());
        }
        auto cursor = editor->document()->find(QStringLiteral("链接")); editor->setTextCursor(cursor);
        const auto point = editor->cursorRect(cursor).center(); QTest::mouseClick(editor->viewport(), Qt::LeftButton, {}, point);
        for (auto *target : {static_cast<QWidget *>(editor), editor->viewport()}) {
            QHelpEvent event(QEvent::ToolTip, point, target->mapToGlobal(point)); QApplication::sendEvent(target, &event);
        }
        QVERIFY(!QToolTip::isVisible()); QVERIFY(window->grab().toImage().size().isValid());
        QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(!QApplication::clipboard()->text().contains(QChar::ObjectReplacementCharacter));
        QCOMPARE(globalLoads, 0); QCOMPARE(instanceLoads, 0); QCOMPARE(probe.resources, 0); QCOMPARE(probe.links, 0);
    }
    void htmlPinClearsEmptyParagraphTooltips() {
        for (const auto &body : {QString("<p style='-qt-paragraph-type:empty' title=\"&lt;img src='file:///private/secret.png'&gt;\"><br /></p>"),
                                QString("<p title=\"&lt;img src='file:///private/secret.png'&gt;\"></p><p>正文</p>")}) {
            SafeHtmlDocument document("<html><head><meta name='qrichtext' content='1' /></head><body>" + body + "</body></html>");
            for (auto block = document.begin(); block.isValid(); block = block.next()) {
                QVERIFY(block.charFormat().toolTip().isEmpty()); QVERIFY(block.charFormat().anchorHref().isEmpty());
            }
        }
    }
    void pinSelectsAndCopiesText() {
        auto *window = pin();
        QTRY_VERIFY(!window->toolTip().isEmpty());
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(window, QPoint(80, 20));
        QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(80, 20));
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello Qt"));
    }
    void restoredPinStaysVisibleWhenItsOldScreenIsMissing() {
        auto *window = pin();
        auto state = window->state();
        state.geometry = QRect(-50000, -50000, 9000, 9000); state.opacity = 0.5; state.alwaysOnTop = false;
        window->restoreLayout(state); window->show();
        QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().contains(window->geometry()));
        QCOMPARE(window->image(), state.image); QCOMPARE(window->state().originalSize, state.originalSize);
        QVERIFY(qAbs(window->windowOpacity() - 0.5) < 0.01); QVERIFY(!(window->windowFlags() & Qt::WindowStaysOnTopHint));
    }
    void compactPinMenuCopiesTheCurrentContent_data() {
        QTest::addColumn<bool>("selectedText"); QTest::newRow("image") << false; QTest::newRow("selected-text") << true;
    }
    void compactPinMenuCopiesTheCurrentContent() {
        QFETCH(bool, selectedText);
        auto *window = pin(); QTRY_VERIFY(!window->toolTip().isEmpty());
        if (selectedText) QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        bool handled = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            auto *menu = window->findChild<QMenu *>("PinMenu"); if (!menu || !menu->isVisible()) return;
            dismiss.stop(); const auto close = qScopeGuard([&] { menu->close(); });
            QStringList primary;
            for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text());
            QCOMPARE(primary, QStringList({selectedText ? QStringLiteral("复制选中文字") : QStringLiteral("复制图片"), QStringLiteral("保存图片…"), QStringLiteral("编辑标注…"), QStringLiteral("更多操作"), QStringLiteral("关闭")}));
            menu->actions().first()->trigger();
            if (selectedText) {
                QCOMPARE(QApplication::clipboard()->text(), QString("Hello Qt\nNext"));
                auto *more = menu->findChild<QMenu *>("PinMoreMenu"); QVERIFY(more);
                QAction *image = nullptr;
                for (auto *action : more->actions()) if (action->text() == QStringLiteral("复制图片")) image = action;
                QVERIFY(image); image->trigger();
            }
            QCOMPARE(QApplication::clipboard()->image(), window->image());
            handled = true;
        });
        dismiss.start(10);
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), window->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(window, &event); QVERIFY(handled);
    }
    void pinSelectionPreservesSpacingInBothDirections() {
        auto *window = pin(whiteImage(420));
        QTRY_VERIFY(!window->toolTip().isEmpty());
        for (bool reverse : {false, true}) {
            const QPoint start = reverse ? QPoint(80, 20) : QPoint(20, 20);
            const QPoint end = reverse ? QPoint(20, 20) : QPoint(80, 20);
            QTest::mousePress(window, Qt::LeftButton, {}, start);
            QTest::mouseMove(window, end);
            QTest::mouseRelease(window, Qt::LeftButton, {}, end);
            copy(window);
            QCOMPARE(QApplication::clipboard()->text(), QString("Wide  \tQt"));
        }
    }
    void pinSelectAllPreservesOuterWhitespace() {
        auto *window = pin(whiteImage(420));
        QTRY_VERIFY(!window->toolTip().isEmpty());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("  Wide  \tQt\r\nNext!  "));
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(20, 20));
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Wide"));
    }
    void pinDragSnapsToRectangleInsteadOfCenter() {
        auto *window = pin(whiteImage(440));
        QTRY_VERIFY(!window->toolTip().isEmpty());
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(20, 10));
        QTest::mouseMove(window, QPoint(201, 28));
        QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(201, 28));
        copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("wide"));
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
        QCoreApplication::processEvents();
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
    void cancellingCloseRestoresHideOnBlur() {
        auto *window = pin();
        window->configure(true, true);
        QTest::mouseDClick(window, Qt::LeftButton, {}, QPoint(20, 20));
        QPointer<QDialog> prompt = window->findChild<QDialog *>("PinClosePrompt");
        QVERIFY(prompt && prompt->isVisible());
        QTest::keyClick(prompt, Qt::Key_Escape);
        QTRY_VERIFY(prompt.isNull());
        QCoreApplication::processEvents();
        QVERIFY(window->isVisible());
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(window, &deactivate);
        QVERIFY(!window->isVisible());
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
        QTRY_VERIFY(!window->toolTip().isEmpty());
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
        QTRY_VERIFY(!window->toolTip().isEmpty());
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
        QTRY_COMPARE(editor->findChildren<OcrResultDialog *>().size(), 1);
    }
    void overlayReturnsDevicePixelsAndLocalGeometry() {
        auto *overlay = new Overlay({whiteImage(600), QRect(-300, 0, 300, 75), {}}, "copy", false);
        QSignalSpy result(overlay, &Overlay::accepted);
        overlay->show();
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(overlay, QPoint(120, 60));
        QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(120, 60));
        QCOMPARE(result.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(result[0][0]).size(), QSize(200, 80));
        QCOMPARE(result[0][1].toRectF(), QRectF(20, 20, 100, 40));
    }
    void overlayPreservesFractionalSelectionForRepeatedCapture() {
        QImage image(240, 180, QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x, y, (x + y) % 256));
        const QPoint origin(-500, -200);
        auto *overlay = new Overlay({image, QRect(origin, QSize(160, 120)), {}}, "copy", false);
        QSignalSpy result(overlay, &Overlay::accepted);
        overlay->show();
        const auto sendMouse = [&](QEvent::Type type, const QPointF &position, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, position, position + QPointF(origin), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(overlay, &event);
        };
        sendMouse(QEvent::MouseButtonPress, {10.25, 12.5}, Qt::LeftButton, Qt::LeftButton);
        sendMouse(QEvent::MouseMove, {30.75, 35.25}, Qt::NoButton, Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, {30.75, 35.25}, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(result.count(), 1);
        QCOMPARE(result[0][1].toRectF(), QRectF(10.25, 12.5, 20.5, 22.75));
        QCOMPARE(qvariant_cast<QImage>(result[0][0]), image.copy(15, 18, 32, 35));
        QCOMPARE(result[0][2].toString(), QString("copy"));
    }
    void overlayCancellationSignalsOnce() {
        auto *overlay = new Overlay({whiteImage(), QRect(0, 0, 300, 150), {}}, "annotate", false);
        QSignalSpy cancelled(overlay, &Overlay::cancelled);
        QSignalSpy accepted(overlay, &Overlay::accepted);
        overlay->show();
        QTest::keyClick(overlay, Qt::Key_Escape);
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(accepted.count(), 0);
    }
    void overlaySizeInputsResizeInImagePixels_data() {
        QTest::addColumn<QSize>("logicalSize"); QTest::addColumn<QSize>("imageSize");
        QTest::addColumn<QSize>("initialSize"); QTest::addColumn<QPoint>("pixelOrigin");
        QTest::newRow("unit") << QSize(640, 480) << QSize(640, 480) << QSize(160, 100) << QPoint(50, 40);
        QTest::newRow("fractional-dpi") << QSize(480, 320) << QSize(720, 480) << QSize(240, 150) << QPoint(75, 60);
        QTest::newRow("reduced-frame") << QSize(1000, 700) << QSize(641, 359) << QSize(103, 52) << QPoint(32, 20);
    }
    void overlaySizeInputsResizeInImagePixels() {
        QFETCH(QSize, logicalSize); QFETCH(QSize, imageSize); QFETCH(QSize, initialSize); QFETCH(QPoint, pixelOrigin);
        QImage image(imageSize, QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
        auto *overlay = new Overlay({image, QRect(QPoint(-500, -200), logicalSize), {}}, "annotate", false); overlay->show();
        QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 40)); QTest::mouseMove(overlay, QPoint(210, 140)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(210, 140));
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        QCOMPARE(QSize(width->value(), height->value()), initialSize); QCOMPARE(width->minimum(), 1); QCOMPARE(height->minimum(), 1);
        QCOMPARE(width->maximum(), image.width()); QCOMPARE(height->maximum(), image.height());
        width->setValue(97); QCOMPARE(height->value(), initialSize.height()); height->setValue(63); QCOMPARE(width->value(), 97);
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.count(), 1);
        const QRect expected(pixelOrigin, QSize(97, 63));
        QCOMPARE(qvariant_cast<QImage>(accepted[0][0]), image.copy(expected));
        QCOMPARE(pixelRect(accepted[0][1].toRectF(), logicalSize, imageSize), expected);
    }
    void overlaySizeInputsKeepRequestedSizeAtEdges_data() {
        QTest::addColumn<QSize>("requested"); QTest::addColumn<QRect>("expected");
        QTest::newRow("move-to-fit") << QSize(120, 100) << QRect(480, 350, 120, 100);
        QTest::newRow("one-pixel") << QSize(1, 1) << QRect(540, 390, 1, 1);
        QTest::newRow("full-frame") << QSize(600, 450) << QRect(0, 0, 600, 450);
    }
    void overlaySizeInputsKeepRequestedSizeAtEdges() {
        QFETCH(QSize, requested); QFETCH(QRect, expected);
        QImage image(600, 450, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *overlay = new Overlay({image, QRect(-500, -200, 400, 300), {}}, "annotate", false); overlay->show();
        QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(360, 260)); QTest::mouseMove(overlay, QPoint(390, 290)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(390, 290));
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        width->setValue(requested.width()); height->setValue(requested.height());
        QCOMPARE(QSize(width->value(), height->value()), requested);
        auto *toolbar = overlay->findChild<QWidget *>("CaptureTools"); QVERIFY(overlay->rect().contains(toolbar->geometry()));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(accepted[0][0]).size(), requested); QCOMPARE(pixelRect(accepted[0][1].toRectF(), overlay->size(), image.size()), expected);
    }
    void overlayThinSelectionCanMove() {
        QImage image(600, 400, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *overlay = new Overlay({image, QRect(0, 0, 600, 400), {}}, "annotate", false);
        QSignalSpy accepted(overlay, &Overlay::accepted); overlay->show();
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 50));
        QTest::mouseMove(overlay, QPoint(150, 150)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(150, 150));
        overlay->findChild<QSpinBox *>("CaptureWidth")->setValue(1);
        overlay->findChild<QSpinBox *>("CaptureHeight")->setValue(80);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 70));
        QTest::mouseMove(overlay, QPoint(60, 80)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(60, 80));
        overlay->findChild<QToolButton *>("captureAction-edit")->click();
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first()[1].toRectF(), QRectF(60, 60, 1, 80));
        QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(60, 60, 1, 80));
    }
    void overlayFreeRatioKeyboardMoveStopsAtBoundary_data() {
        QTest::addColumn<QRect>("region");
        QTest::addColumn<int>("horizontal");
        QTest::addColumn<int>("vertical");
        QTest::addColumn<QRect>("expected");
        QTest::newRow("top-left") << QRect(3, 4, 160, 90) << int(Qt::Key_Left) << int(Qt::Key_Up) << QRect(0, 0, 160, 90);
        QTest::newRow("bottom-right") << QRect(477, 386, 160, 90) << int(Qt::Key_Right) << int(Qt::Key_Down) << QRect(480, 390, 160, 90);
    }
    void overlayFreeRatioKeyboardMoveStopsAtBoundary() {
        QFETCH(QRect, region); QFETCH(int, horizontal); QFETCH(int, vertical); QFETCH(QRect, expected);
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false);
        overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, region.topLeft());
        const QPoint end = region.topLeft() + QPoint(region.width(), region.height());
        QTest::mouseMove(overlay, end); QTest::mouseRelease(overlay, Qt::LeftButton, {}, end);
        for (int i = 0; i < 2; ++i) {
            QTest::keyClick(overlay, Qt::Key(horizontal), Qt::AltModifier | Qt::ShiftModifier);
            QTest::keyClick(overlay, Qt::Key(vertical), Qt::AltModifier | Qt::ShiftModifier);
        }
        overlay->findChild<QToolButton *>("captureAction-copy")->click();
        QCOMPARE(accepted.size(), 1); QCOMPARE(accepted.first()[1].toRectF(), QRectF(expected));
        QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(expected));
    }
    void overlaySizeInputsFollowHandlesAndKeyboard() {
        auto *overlay = new Overlay({whiteImage(640), QRect(0, 0, 640, 150), {}}, "annotate", false); overlay->show();
        QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 20)); QTest::mouseMove(overlay, QPoint(300, 80)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(300, 80));
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(300, 80)); QTest::mouseMove(overlay, QPoint(340, 100)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(340, 100));
        QCOMPARE(QSize(width->value(), height->value()), QSize(240, 80));
        QTest::keyClick(overlay, Qt::Key_Right); QTest::keyClick(overlay, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(QSize(width->value(), height->value()), QSize(241, 70));
        QTest::keyClick(overlay, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier); QCOMPARE(QSize(width->value(), height->value()), QSize(241, 70));
        QTest::keyClick(overlay, Qt::Key_Return); QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted[0][1].toRectF(), QRectF(110, 20, 241, 70));
    }
    void overlaySizeInputFocusDoesNotFinishCapture() {
        QImage image(640, 480, QImage::Format_ARGB32); image.fill(Qt::white);
        QPointer<Overlay> overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show();
        QSignalSpy accepted(overlay, &Overlay::accepted); QSignalSpy cancelled(overlay, &Overlay::cancelled);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(300, 220)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(300, 220));
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        width->setFocus(); QTRY_VERIFY(width->hasFocus()); width->selectAll(); QTest::keyClicks(width, "123"); QCOMPARE(width->value(), 200);
        QTest::keyClick(width, Qt::Key_Return); QCOMPARE(width->value(), 123); QCOMPARE(accepted.count(), 0);
        QTest::keyClick(width, Qt::Key_Tab); QTRY_VERIFY(height->hasFocus());
        for (const auto &key : {Qt::Key_S, Qt::Key_P, Qt::Key_O}) QTest::keyClick(height, key, Qt::ControlModifier);
        QTest::keyClick(height, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(accepted.count(), 0);
        QTest::keyClick(height, Qt::Key_Escape); QTRY_VERIFY(overlay.isNull()); QCOMPARE(cancelled.count(), 1); QCOMPARE(accepted.count(), 0);
    }
    void overlayEmptySelectionHidesSizeInputs() {
        auto *overlay = new Overlay({whiteImage(640), QRect(0, 0, 640, 150), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(60, 60)); QTest::mouseMove(overlay, QPoint(70, 80)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(70, 80));
        QTest::keyClick(overlay, Qt::Key_Left, Qt::ShiftModifier); QVERIFY(!overlay->findChild<QWidget *>("CaptureTools")->isVisible());
        QTest::keyClick(overlay, Qt::Key_Return); QCOMPARE(accepted.count(), 0);
    }
    void overlayButtonCommitsPendingSizeText() {
        QImage image(640, 480, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show();
        QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(300, 220)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(300, 220));
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); QVERIFY(width); width->setFocus(); QTRY_VERIFY(width->hasFocus());
        width->selectAll(); QTest::keyClicks(width, "211"); QCOMPARE(width->value(), 200);
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(accepted[0][0]).size(), QSize(211, 120));
    }
    void overlayCanvasClearingDoesNotRevivePendingSize() {
        QImage image(640, 480, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show();
        QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(300, 220)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(300, 220));
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        width->setFocus(); QTRY_VERIFY(width->hasFocus()); width->selectAll(); QTest::keyClicks(width, "250");
        QTest::mouseClick(overlay, Qt::RightButton, {}, QPoint(450, 400)); auto *toolbar = overlay->findChild<QWidget *>("CaptureTools"); QVERIFY(!toolbar->isVisible());
        QTest::keyClick(overlay, Qt::Key_Return); QCOMPARE(accepted.count(), 0); QCoreApplication::processEvents(); QVERIFY(!toolbar->isVisible());
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(60, 60)); QTest::mouseMove(overlay, QPoint(180, 140)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(180, 140));
        QCOMPARE(QSize(width->value(), height->value()), QSize(120, 80));
        auto labels = toolbar->findChildren<QLabel *>(); QVERIFY(!labels.isEmpty()); QTest::mouseClick(labels.first(), Qt::LeftButton);
        QTest::mouseClick(toolbar, Qt::LeftButton, {}, QPoint(toolbar->width() - 2, 2)); QVERIFY(toolbar->isVisible()); QCOMPARE(QSize(width->value(), height->value()), QSize(120, 80));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.count(), 1); QCOMPARE(qvariant_cast<QImage>(accepted[0][0]).size(), QSize(120, 80));
    }
    void overlayQuickModesAllowShiftSizeAdjustment_data() {
        QTest::addColumn<QString>("mode"); QTest::newRow("copy") << QString("copy"); QTest::newRow("scroll") << QString("scroll");
    }
    void overlayQuickModesAllowShiftSizeAdjustment() {
        QFETCH(QString, mode);
        QImage image(640, 480, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, mode, false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(300, 220)); QTest::mouseRelease(overlay, Qt::LeftButton, Qt::ShiftModifier, QPoint(300, 220));
        QCOMPARE(accepted.count(), 0); QVERIFY(overlay->findChild<QWidget *>("CaptureTools")->isVisible());
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(200, 160)); QTest::mouseMove(overlay, QPoint(210, 170)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(210, 170));
        QCOMPARE(accepted.count(), 0);
        auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); QVERIFY(width); width->setValue(111);
        QTest::keyClick(overlay, Qt::Key_Return); QCOMPARE(accepted.count(), 1); QCOMPARE(accepted[0][2].toString(), mode); QCOMPARE(qvariant_cast<QImage>(accepted[0][0]).size(), QSize(111, 120));
    }
    void overlayRatioInputsProduceExactPixelCrops_data() {
        QTest::addColumn<int>("ratioIndex"); QTest::addColumn<QSize>("ratio");
        QTest::addColumn<QSize>("logicalSize"); QTest::addColumn<QSize>("imageSize"); QTest::addColumn<QRect>("expected");
        QTest::newRow("square") << 1 << QSize(1, 1) << QSize(640, 480) << QSize(640, 480) << QRect(50, 40, 101, 101);
        QTest::newRow("landscape-4-3") << 2 << QSize(4, 3) << QSize(640, 480) << QSize(640, 480) << QRect(50, 40, 100, 75);
        QTest::newRow("landscape-16-9") << 3 << QSize(16, 9) << QSize(640, 480) << QSize(640, 480) << QRect(50, 40, 96, 54);
        QTest::newRow("portrait-3-4") << 4 << QSize(3, 4) << QSize(640, 480) << QSize(640, 480) << QRect(50, 40, 102, 136);
        QTest::newRow("portrait-9-16") << 5 << QSize(9, 16) << QSize(640, 480) << QSize(640, 480) << QRect(50, 40, 99, 176);
        QTest::newRow("fractional-negative-origin") << 3 << QSize(16, 9) << QSize(480, 320) << QSize(720, 480) << QRect(75, 60, 96, 54);
        QTest::newRow("reduced-nonuniform-frame") << 3 << QSize(16, 9) << QSize(1000, 700) << QSize(641, 359) << QRect(32, 20, 96, 54);
    }
    void overlayRatioInputsProduceExactPixelCrops() {
        QFETCH(int, ratioIndex); QFETCH(QSize, ratio); QFETCH(QSize, logicalSize); QFETCH(QSize, imageSize); QFETCH(QRect, expected);
        const auto image = coordinateImage(imageSize);
        auto *overlay = new Overlay({image, QRect(QPoint(-500, -200), logicalSize), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 40)); QTest::mouseMove(overlay, QPoint(210, 140)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(210, 140));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratios); QVERIFY(width); QVERIFY(height);
        QCOMPARE(ratios->count(), 6); QCOMPARE(ratios->currentIndex(), 0); QCOMPARE(ratios->itemData(ratioIndex).toSize(), ratio);
        ratios->setCurrentIndex(ratioIndex); width->setValue(101);
        QCOMPARE(QSize(width->value(), height->value()), expected.size()); QCOMPARE(width->singleStep(), ratio.width()); QCOMPARE(height->singleStep(), ratio.height());
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        const auto output = qvariant_cast<QImage>(accepted.first()[0]); QCOMPARE(output, image.copy(expected));
        QCOMPARE(pixelRect(accepted.first()[1].toRectF(), logicalSize, imageSize), expected);
        QCOMPARE(output.width() * ratio.height(), output.height() * ratio.width());
    }
    void overlayRatioOnlyOffersUnitsThatFitFrame() {
        const auto image = coordinateImage(QSize(8, 5));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 400), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(80, 80)); QTest::mouseMove(overlay, QPoint(400, 320)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(400, 320));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratios); QVERIFY(width); QVERIFY(height);
        QCOMPARE(ratios->count(), 4); QCOMPARE(ratios->itemData(1).toSize(), QSize(1, 1)); QCOMPARE(ratios->itemData(2).toSize(), QSize(4, 3)); QCOMPARE(ratios->itemData(3).toSize(), QSize(3, 4));
        ratios->setCurrentIndex(2); QCOMPARE(QSize(width->value(), height->value()), QSize(4, 3)); QCOMPARE(width->minimum(), 4); QCOMPARE(width->maximum(), 4);
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(1, 1, 4, 3));
    }
    void overlayRatioInputsStepClampAndReturnToFree() {
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 80)); QTest::mouseMove(overlay, QPoint(420, 260)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(420, 260));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratios); QVERIFY(width); QVERIFY(height);
        ratios->setCurrentIndex(3); QCOMPARE(width->minimum(), 16); QCOMPARE(height->minimum(), 9); QCOMPARE(width->maximum(), 640); QCOMPARE(height->maximum(), 360);
        width->setFocus(); QTRY_VERIFY(width->hasFocus()); width->selectAll(); QTest::keyClicks(width, "401"); QCOMPARE(width->value(), 320);
        QTest::keyClick(width, Qt::Key_Return); QCOMPARE(QSize(width->value(), height->value()), QSize(400, 225)); QCOMPARE(width->cleanText(), QString("400")); QCOMPARE(height->cleanText(), QString("225")); QVERIFY(accepted.isEmpty());
        QTest::keyClick(width, Qt::Key_Up); QCOMPARE(QSize(width->value(), height->value()), QSize(416, 234));
        QTest::keyClick(height, Qt::Key_Down); QCOMPARE(QSize(width->value(), height->value()), QSize(400, 225));
        height->setValue(1); QCOMPARE(QSize(width->value(), height->value()), QSize(16, 9));
        width->setValue(10000); height->setValue(10000); QCOMPARE(QSize(width->value(), height->value()), QSize(640, 360));
        ratios->setCurrentIndex(0); QCOMPARE(width->minimum(), 1); QCOMPARE(height->minimum(), 1); QCOMPARE(width->maximum(), 640); QCOMPARE(height->maximum(), 480); QCOMPARE(width->singleStep(), 1); QCOMPARE(height->singleStep(), 1);
        width->setValue(211); height->setValue(97); QCOMPARE(QSize(width->value(), height->value()), QSize(211, 97));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(0, 80, 211, 97));
        QCOMPARE(accepted.first()[1].toRectF(), QRectF(0, 80, 211, 97));
    }
    void overlayRatioInputsMoveToFitAtEdges() {
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(560, 380)); QTest::mouseMove(overlay, QPoint(620, 440)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(620, 440));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); QVERIFY(ratios); QVERIFY(width);
        ratios->setCurrentIndex(3); width->setValue(401);
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first()[1].toRectF(), QRectF(240, 255, 400, 225)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(240, 255, 400, 225));
    }
    void overlayRatioHandlesKeepOppositeAnchors_data() {
        QTest::addColumn<QRect>("initial"); QTest::addColumn<QPoint>("from"); QTest::addColumn<QPoint>("to"); QTest::addColumn<QRect>("expected");
        const QRect initial(200, 150, 160, 90);
        QTest::newRow("top-left") << initial << QPoint(200, 150) << QPoint(168, 132) << QRect(168, 132, 192, 108);
        QTest::newRow("top") << initial << QPoint(280, 150) << QPoint(280, 132) << QRect(184, 132, 192, 108);
        QTest::newRow("top-right") << initial << QPoint(360, 150) << QPoint(392, 132) << QRect(200, 132, 192, 108);
        QTest::newRow("right") << initial << QPoint(360, 195) << QPoint(392, 195) << QRect(200, 141, 192, 108);
        QTest::newRow("bottom-right") << initial << QPoint(360, 240) << QPoint(392, 258) << QRect(200, 150, 192, 108);
        QTest::newRow("bottom") << initial << QPoint(280, 240) << QPoint(280, 258) << QRect(184, 150, 192, 108);
        QTest::newRow("bottom-left") << initial << QPoint(200, 240) << QPoint(168, 258) << QRect(168, 150, 192, 108);
        QTest::newRow("left") << initial << QPoint(200, 195) << QPoint(168, 195) << QRect(168, 141, 192, 108);
        QTest::newRow("cross-opposite-corner") << initial << QPoint(360, 240) << QPoint(168, 132) << QRect(168, 132, 32, 18);
        QTest::newRow("stop-at-image-edge") << initial << QPoint(360, 240) << QPoint(640, 480) << QRect(200, 150, 432, 243);
        QTest::newRow("no-room-across-anchor") << QRect(5, 100, 160, 90) << QPoint(165, 190) << QPoint(0, 50) << QRect(5, 100, 160, 90);
    }
    void overlayRatioHandlesKeepOppositeAnchors() {
        QFETCH(QRect, initial); QFETCH(QPoint, from); QFETCH(QPoint, to); QFETCH(QRect, expected);
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, initial.topLeft()); QTest::mouseMove(overlay, initial.topLeft() + QPoint(initial.width(), initial.height())); QTest::mouseRelease(overlay, Qt::LeftButton, {}, initial.topLeft() + QPoint(initial.width(), initial.height()));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); QVERIFY(ratios); ratios->setCurrentIndex(3);
        QTest::mousePress(overlay, Qt::LeftButton, {}, from); QTest::mouseMove(overlay, to); QTest::mouseRelease(overlay, Qt::LeftButton, {}, to);
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first()[1].toRectF(), QRectF(expected)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(expected));
    }
    void overlayRatioMovesPreservePixelSize_data() {
        QTest::addColumn<QSize>("logicalSize"); QTest::addColumn<QSize>("imageSize"); QTest::addColumn<QPoint>("dragStart"); QTest::addColumn<QRect>("expected");
        QTest::newRow("unit") << QSize(640, 480) << QSize(640, 480) << QPoint(130, 85) << QRect(67, 59, 160, 90);
        QTest::newRow("fractional") << QSize(480, 320) << QSize(720, 480) << QPoint(103, 70) << QRect(96, 84, 160, 90);
        QTest::newRow("reduced-nonuniform") << QSize(1000, 700) << QSize(641, 359) << QPoint(175, 127) << QRect(46, 35, 160, 90);
    }
    void overlayRatioMovesPreservePixelSize() {
        QFETCH(QSize, logicalSize); QFETCH(QSize, imageSize); QFETCH(QPoint, dragStart); QFETCH(QRect, expected);
        const auto image = coordinateImage(imageSize);
        auto *overlay = new Overlay({image, QRect(QPoint(-500, -200), logicalSize), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 40)); QTest::mouseMove(overlay, QPoint(210, 140)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(210, 140));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratios); QVERIFY(width); QVERIFY(height);
        ratios->setCurrentIndex(3); width->setValue(160);
        QTest::mousePress(overlay, Qt::LeftButton, {}, dragStart); QTest::mouseMove(overlay, dragStart + QPoint(7, 9)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, dragStart + QPoint(7, 9));
        QTest::keyClick(overlay, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier); QTest::keyClick(overlay, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(QSize(width->value(), height->value()), QSize(160, 90));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        QCOMPARE(pixelRect(accepted.first()[1].toRectF(), logicalSize, imageSize), expected); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(expected));
    }
    void overlayRatioAltMovesReducedFrameByPixels_data() {
        QTest::addColumn<int>("key"); QTest::addColumn<int>("repeat"); QTest::addColumn<bool>("fast"); QTest::addColumn<QRect>("expected");
        QTest::newRow("right-repeat") << int(Qt::Key_Right) << 3 << false << QRect(35, 32, 64, 36);
        QTest::newRow("down-repeat") << int(Qt::Key_Down) << 2 << false << QRect(32, 34, 64, 36);
        QTest::newRow("left-repeat") << int(Qt::Key_Left) << 3 << false << QRect(29, 32, 64, 36);
        QTest::newRow("up-repeat") << int(Qt::Key_Up) << 2 << false << QRect(32, 30, 64, 36);
        QTest::newRow("shift-right-repeat") << int(Qt::Key_Right) << 2 << true << QRect(52, 32, 64, 36);
        QTest::newRow("shift-down") << int(Qt::Key_Down) << 1 << true << QRect(32, 42, 64, 36);
        QTest::newRow("shift-left") << int(Qt::Key_Left) << 1 << true << QRect(22, 32, 64, 36);
        QTest::newRow("shift-up") << int(Qt::Key_Up) << 1 << true << QRect(32, 22, 64, 36);
    }
    void overlayRatioAltMovesReducedFrameByPixels() {
        QFETCH(int, key); QFETCH(int, repeat); QFETCH(bool, fast); QFETCH(QRect, expected);
        const auto image = coordinateImage(QSize(320, 224));
        auto *overlay = new Overlay({image, QRect(-500, -200, 1000, 700), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(600, 400)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(600, 400));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); QVERIFY(ratios); QVERIFY(width); ratios->setCurrentIndex(3); width->setValue(64);
        for (int i = 0; i < repeat; ++i) QTest::keyClick(overlay, Qt::Key(key), Qt::AltModifier | (fast ? Qt::ShiftModifier : Qt::NoModifier));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        QCOMPARE(pixelRect(accepted.first()[1].toRectF(), QSize(1000, 700), image.size()), expected); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(expected));
    }
    void overlayRatioMoveStopsAtBoundaryWithoutCropping() {
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 80)); QTest::mouseMove(overlay, QPoint(260, 170)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(260, 170));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); QVERIFY(ratios); ratios->setCurrentIndex(3);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(180, 125)); QTest::mouseMove(overlay, QPoint(640, 480)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(640, 480));
        QTest::keyClick(overlay, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier); QTest::keyClick(overlay, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first()[1].toRectF(), QRectF(480, 390, 160, 90)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(480, 390, 160, 90));
    }
    void overlayRatioKeyboardUsesRatioUnits() {
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 80)); QTest::mouseMove(overlay, QPoint(420, 260)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(420, 260));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratios); QVERIFY(width); QVERIFY(height); ratios->setCurrentIndex(3);
        QTest::keyClick(overlay, Qt::Key_Right); QCOMPARE(QSize(width->value(), height->value()), QSize(336, 189));
        QTest::keyClick(overlay, Qt::Key_Up, Qt::ShiftModifier); QCOMPARE(QSize(width->value(), height->value()), QSize(176, 99));
        QTest::keyClick(overlay, Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier); QTest::keyClick(overlay, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
        QTest::keyClick(overlay, Qt::Key_Down); QCOMPARE(QSize(width->value(), height->value()), QSize(192, 108));
        QTest::keyClick(overlay, Qt::Key_Left, Qt::ShiftModifier); QTest::keyClick(overlay, Qt::Key_Left, Qt::ShiftModifier); QCOMPARE(QSize(width->value(), height->value()), QSize(16, 9));
        QTest::keyClick(overlay, Qt::Key_Return); QCOMPARE(accepted.size(), 1); QCOMPARE(accepted.first()[1].toRectF(), QRectF(90, 70, 16, 9)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(90, 70, 16, 9));
    }
    void overlayRatioSurvivesRedrawAndWindowSnap() {
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {QRect(400, 200, 160, 100)}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(260, 200)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(260, 200));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); auto *height = overlay->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratios); QVERIFY(width); QVERIFY(height); ratios->setCurrentIndex(2);
        QTest::mouseClick(overlay, Qt::RightButton, {}, QPoint(500, 400)); QVERIFY(!overlay->findChild<QWidget *>("CaptureTools")->isVisible());
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 40)); QTest::mouseMove(overlay, QPoint(210, 160)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(210, 160));
        QCOMPARE(ratios->currentIndex(), 2); QCOMPARE(QSize(width->value(), height->value()), QSize(160, 120)); QVERIFY(accepted.isEmpty());
        QTest::mouseClick(overlay, Qt::RightButton, {}, QPoint(500, 400)); QTest::mouseMove(overlay, QPoint(450, 250)); QTest::mouseClick(overlay, Qt::LeftButton, {}, QPoint(450, 250));
        QCOMPARE(QSize(width->value(), height->value()), QSize(160, 120));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1); QCOMPARE(accepted.first()[1].toRectF(), QRectF(400, 200, 160, 120)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(400, 200, 160, 120));
    }
    void overlayRatioWindowPreviewMatchesConfirmedRegion() {
        const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {QRect(400, 200, 160, 100)}}, "annotate", false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(50, 40)); QTest::mouseMove(overlay, QPoint(210, 160)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(210, 160));
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); QVERIFY(ratios); ratios->setCurrentIndex(1);
        QTest::mouseClick(overlay, Qt::RightButton, {}, QPoint(500, 400));
        QMouseEvent hover(QEvent::MouseMove, QPointF(450, 250), QPointF(450, 250), Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(overlay, &hover);
        const auto preview = overlay->grab().toImage(); QCOMPARE(preview.pixelColor(450, 330), image.pixelColor(450, 330));
        QTest::mouseClick(overlay, Qt::LeftButton, {}, QPoint(450, 250));
        overlay->findChild<QToolButton *>("captureAction-copy")->click(); QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first()[1].toRectF(), QRectF(400, 200, 160, 160)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(400, 200, 160, 160));
    }
    void overlayRatioQuickModesWaitForConfirmation_data() { overlayQuickModesAllowShiftSizeAdjustment_data(); }
    void overlayRatioQuickModesWaitForConfirmation() {
        QFETCH(QString, mode); const auto image = coordinateImage(QSize(640, 480));
        auto *overlay = new Overlay({image, QRect(0, 0, 640, 480), {}}, mode, false); overlay->show(); QSignalSpy accepted(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(100, 100)); QTest::mouseMove(overlay, QPoint(300, 220)); QTest::mouseRelease(overlay, Qt::LeftButton, Qt::ShiftModifier, QPoint(300, 220)); QVERIFY(accepted.isEmpty());
        auto *ratios = overlay->findChild<QComboBox *>("CaptureAspectRatio"); auto *width = overlay->findChild<QSpinBox *>("CaptureWidth"); QVERIFY(ratios); QVERIFY(width); ratios->setCurrentIndex(2); width->setValue(401);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(250, 250)); QTest::mouseMove(overlay, QPoint(257, 259)); QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(257, 259)); QVERIFY(accepted.isEmpty());
        QTest::keyClick(overlay, Qt::Key_Return); QCOMPARE(accepted.size(), 1); QCOMPARE(accepted.first()[2].toString(), mode); QCOMPARE(accepted.first()[1].toRectF(), QRectF(107, 109, 400, 300)); QCOMPARE(qvariant_cast<QImage>(accepted.first()[0]), image.copy(107, 109, 400, 300));
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
    void annotationSelectionUsesToolbarAndVisibleShapes() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->show(); editor->activateWindow();
        auto *canvas = editor->findChild<Canvas *>();
        auto *toolbar = editor->findChild<QToolBar *>();
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        auto *rectangle = editor->findChild<QAction *>("annotationTool-0");
        auto *editText = editor->findChild<QAction *>("annotationTextEdit");
        QVERIFY(canvas); QVERIFY(toolbar); QVERIFY(select); QVERIFY(rectangle); QVERIFY(editText);
        QVERIFY(!canvas->selectionMode()); QVERIFY(rectangle->isChecked()); QVERIFY(!editText->isEnabled());
        drawAnnotation(canvas, {20, 20}, {140, 110});
        drawAnnotation(canvas, {20, 20}, {140, 110});
        QCOMPARE(canvas->document.annotations().size(), 2);
        const auto before = canvas->document.annotations();
        QTest::mouseClick(toolbar->widgetForAction(select), Qt::LeftButton);
        QVERIFY(canvas->selectionMode()); QVERIFY(select->isChecked()); QVERIFY(!rectangle->isChecked());
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {80, 60}));
        QCOMPARE(canvas->selectedAnnotation(), -1);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 60}));
        QCOMPARE(canvas->selectedAnnotation(), 1);
        QCOMPARE(canvas->document.annotations(), before);
        QVERIFY(!editText->isEnabled());
        QTest::mouseClick(toolbar->widgetForAction(rectangle), Qt::LeftButton);
        QVERIFY(!canvas->selectionMode()); QCOMPARE(canvas->selectedAnnotation(), -1);
        QVERIFY(rectangle->isChecked()); QVERIFY(!select->isChecked());
        QTRY_VERIFY(editor->isActiveWindow());
        QTest::keyClick(canvas, Qt::Key_V);
        QVERIFY(canvas->selectionMode()); QVERIFY(select->isChecked());
        QCOMPARE(canvas->document.annotations(), before);
    }
    void selectedAnnotationMoveUsesSourcePixelsAndOneHistoryEntry() {
        Canvas canvas(whiteImage());
        canvas.resize(632, 332); canvas.show();
        QCOMPARE(canvas.imageRect().size(), QSizeF(600, 300));
        drawAnnotation(&canvas, {20, 20}, {80, 60});
        const auto original = canvas.document.annotations();
        canvas.setSelectionMode(true);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {20, 40}));
        QTest::mouseMove(&canvas, canvasPoint(&canvas, {50, 65}));
        QCOMPARE(canvas.document.annotations(), original);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {50, 65}));
        QCOMPARE(canvas.selectedAnnotation(), 0);
        QCOMPARE(canvas.document.annotations().size(), 1);
        QCOMPARE(canvas.document.annotations()[0].points, QVector<QPointF>({{50, 45}, {110, 85}}));
        const auto moved = canvas.document.annotations();
        canvas.undo(); QCOMPARE(canvas.selectedAnnotation(), -1); QCOMPARE(canvas.document.annotations(), original);
        canvas.undo(); QVERIFY(canvas.document.annotations().isEmpty());
        canvas.redo(); QCOMPARE(canvas.document.annotations(), original);
        canvas.redo(); QCOMPARE(canvas.document.annotations(), moved); QCOMPARE(canvas.selectedAnnotation(), -1);
        canvas.undo();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {20, 40}));
        QTest::keyClick(&canvas, Qt::Key_Right);
        QCOMPARE(canvas.document.annotations()[0].points, QVector<QPointF>({{21, 20}, {81, 60}}));
        QVERIFY(!canvas.document.canRedo());
        canvas.undo(); QCOMPARE(canvas.document.annotations(), original);
    }
    void selectedAnnotationClampAndKeyboardKeepAllPointsTogether() {
        Canvas canvas(whiteImage());
        canvas.resize(632, 332); canvas.show();
        canvas.setTool(Tool::Pen);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {20, 20}));
        QTest::mouseMove(&canvas, canvasPoint(&canvas, {80, 100}));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {50, 60}));
        const auto original = canvas.document.annotations()[0];
        canvas.setSelectionMode(true);
        drawAnnotation(&canvas, {80, 100}, {300, 150});
        QCOMPARE(canvas.document.annotations()[0].points.size(), original.points.size());
        for (int i = 0; i < original.points.size(); ++i)
            QCOMPARE(canvas.document.annotations()[0].points[i], original.points[i] + QPointF(220, 50));
        const auto clamped = canvas.document.annotations();
        QTest::keyClick(&canvas, Qt::Key_Right);
        QCOMPARE(canvas.document.annotations(), clamped);
        QTest::keyClick(&canvas, Qt::Key_Left);
        QTest::keyClick(&canvas, Qt::Key_Up, Qt::ShiftModifier);
        for (int i = 0; i < original.points.size(); ++i)
            QCOMPARE(canvas.document.annotations()[0].points[i], original.points[i] + QPointF(219, 40));
        canvas.undo();
        for (int i = 0; i < original.points.size(); ++i)
            QCOMPARE(canvas.document.annotations()[0].points[i], original.points[i] + QPointF(219, 50));
        canvas.undo(); QCOMPARE(canvas.document.annotations(), clamped);
        canvas.undo(); QCOMPARE(canvas.document.annotations()[0], original);
    }
    void selectedAnnotationStylesUseExistingControlsAndUndo() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        auto *color = editor->findChild<QAction *>("annotationColor");
        auto *width = editor->findChild<QSpinBox *>("annotationWidth");
        QVERIFY(canvas); QVERIFY(select); QVERIFY(color); QVERIFY(width);
        auto *widthText = width->findChild<QLineEdit *>(); QVERIFY(widthText);
        QVERIFY(widthText->width() >= widthText->fontMetrics().horizontalAdvance(width->text()));
        drawAnnotation(canvas, {20, 20}, {80, 60});
        canvas->setColor(Qt::green); canvas->setLineWidth(7);
        drawAnnotation(canvas, {160, 20}, {240, 70});
        const auto original = canvas->document.annotations();
        select->trigger();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        QCOMPARE(width->value(), 3); QCOMPARE(canvas->color, original[0].color);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {160, 40}));
        QCOMPARE(width->value(), 7); QCOMPARE(canvas->color, QColor(Qt::green));
        QCOMPARE(canvas->document.annotations(), original);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        width->setFocus(); width->selectAll(); QTest::keyClick(width, Qt::Key_Delete);
        QCOMPARE(canvas->document.annotations(), original);
        width->setValue(9);
        QCOMPARE(canvas->document.annotations()[0].width, qreal(9));
        QCOMPARE(canvas->document.annotations()[1], original[1]);
        const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard([=] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); });
        bool opened = false; QTimer chooseColor;
        connect(&chooseColor, &QTimer::timeout, this, [&] {
            auto *dialog = editor->findChild<QColorDialog *>(); if (!dialog || !dialog->isVisible()) return;
            chooseColor.stop(); opened = true;
            dialog->setCurrentColor(Qt::blue); dialog->accept();
        });
        chooseColor.start(10); color->trigger(); QVERIFY(opened);
        QCOMPARE(canvas->document.annotations()[0].color, QColor(Qt::blue));
        QCOMPARE(canvas->document.annotations()[1], original[1]);
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0].color, original[0].color);
        QCOMPARE(canvas->document.annotations()[0].width, qreal(9));
        canvas->undo(); QCOMPARE(canvas->document.annotations(), original);
        canvas->redo(); canvas->redo();
        QCOMPARE(canvas->document.annotations()[0].color, QColor(Qt::blue));
        QCOMPARE(canvas->document.annotations()[0].width, qreal(9));
    }
    void annotationTextEditAcceptCancelAndEmptyText() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->show(); editor->activateWindow();
        auto *canvas = editor->findChild<Canvas *>();
        auto *text = editor->findChild<QAction *>("annotationTool-9");
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        auto *edit = editor->findChild<QAction *>("annotationTextEdit");
        QVERIFY(canvas); QVERIFY(text); QVERIFY(select); QVERIFY(edit);
        auto reply = [&](const QString &initial, const QString &replacement, bool accept, auto open) {
            bool opened = false; QTimer answer;
            connect(&answer, &QTimer::timeout, this, [&] {
                auto *dialog = canvas->findChild<QInputDialog *>(); if (!dialog || !dialog->isVisible()) return;
                answer.stop(); opened = true;
                const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
                QCOMPARE(dialog->textValue(), initial);
                dialog->setTextValue(replacement);
                if (accept) dialog->accept();
            });
            answer.start(10); open(); QVERIFY(opened);
        };
        text->trigger();
        reply({}, QStringLiteral("first\nline"), true, [&] { QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 20})); });
        QCOMPARE(canvas->document.annotations().size(), 1);
        const auto original = canvas->document.annotations()[0];
        QCOMPARE(original.text, QStringLiteral("first\nline"));
        select->trigger();
        const auto point = canvasPoint(canvas, annotationBounds(original, canvas->document.original().size()).center());
        QTest::mouseClick(canvas, Qt::LeftButton, {}, point); QVERIFY(edit->isEnabled());
        editor->activateWindow();
        QTRY_VERIFY(editor->isActiveWindow());
        reply(original.text, QStringLiteral("changed 😀\nsecond"), true, [&] { QTest::keyClick(canvas, Qt::Key_F2); });
        auto changed = original; changed.text = QStringLiteral("changed 😀\nsecond");
        QCOMPARE(canvas->document.annotations()[0], changed);
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0], original); QVERIFY(!edit->isEnabled());
        canvas->redo(); QCOMPARE(canvas->document.annotations()[0], changed);
        const auto changedPoint = canvasPoint(canvas, annotationBounds(changed, canvas->document.original().size()).center());
        reply(changed.text, QStringLiteral("discard this"), false, [&] { QTest::mouseDClick(canvas, Qt::LeftButton, {}, changedPoint); });
        QCOMPARE(canvas->document.annotations()[0], changed);
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0], original);
        canvas->redo();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, changedPoint);
        reply(changed.text, {}, true, [&] { edit->trigger(); });
        QVERIFY(canvas->document.annotations().isEmpty()); QCOMPARE(canvas->selectedAnnotation(), -1);
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0], changed);
    }
    void annotationNumberCanBeEditedAndDeletedAsALabel() {
        auto *editor = new Editor(whiteImage(), {}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *number = editor->findChild<QAction *>("annotationTool-10");
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        auto *edit = editor->findChild<QAction *>("annotationTextEdit");
        QVERIFY(canvas); QVERIFY(number); QVERIFY(select); QVERIFY(edit);
        number->trigger(); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {70, 70}));
        QCOMPARE(canvas->document.annotations().size(), 1);
        const auto original = canvas->document.annotations()[0]; QCOMPARE(original.text, QString("1"));
        select->trigger(); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {70, 70}));
        QVERIFY(edit->isEnabled());
        bool opened = false; QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            auto *dialog = canvas->findChild<QInputDialog *>(); if (!dialog || !dialog->isVisible()) return;
            answer.stop(); opened = true;
            const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
            QCOMPARE(dialog->textValue(), QString("1")); dialog->setTextValue("42"); dialog->accept();
        });
        answer.start(10); QTest::mouseDClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {70, 70})); QVERIFY(opened);
        auto changed = original; changed.text = "42";
        QCOMPARE(canvas->document.annotations()[0], changed);
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0], original);
        canvas->redo(); QCOMPARE(canvas->document.annotations()[0], changed);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {70, 70}));
        disconnect(&answer, nullptr, this, nullptr); opened = false;
        connect(&answer, &QTimer::timeout, this, [&] {
            auto *dialog = canvas->findChild<QInputDialog *>(); if (!dialog || !dialog->isVisible()) return;
            answer.stop(); opened = true;
            const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
            QCOMPARE(dialog->textValue(), QString("42")); dialog->setTextValue({}); dialog->accept();
        });
        answer.start(10); edit->trigger(); QVERIFY(opened);
        QVERIFY(canvas->document.annotations().isEmpty());
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0], changed);
    }
    void annotationDeleteRemovesSelectionAndKeepsDrawingUndo() {
        auto *editor = new Editor(whiteImage(), {}, "en-US"); editor->show(); editor->activateWindow();
        auto *canvas = editor->findChild<Canvas *>();
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        auto *rectangle = editor->findChild<QAction *>("annotationTool-0");
        QVERIFY(canvas); QVERIFY(select); QVERIFY(rectangle);
        drawAnnotation(canvas, {20, 20}, {60, 60});
        drawAnnotation(canvas, {100, 20}, {140, 60});
        drawAnnotation(canvas, {200, 20}, {240, 60});
        const auto original = canvas->document.annotations();
        select->trigger(); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        QTRY_VERIFY(editor->isActiveWindow());
        QTest::keyClick(canvas, Qt::Key_Delete);
        QCOMPARE(canvas->document.annotations(), QVector<Annotation>({original[1], original[2]}));
        canvas->undo(); QCOMPARE(canvas->document.annotations(), original); QCOMPARE(canvas->selectedAnnotation(), -1);
        QTest::keyClick(canvas, Qt::Key_Delete); QCOMPARE(canvas->document.annotations(), original);
        rectangle->trigger(); QTest::keyClick(canvas, Qt::Key_Delete);
        QCOMPARE(canvas->document.annotations(), QVector<Annotation>({original[0], original[1]}));
        canvas->redo(); QCOMPARE(canvas->document.annotations(), original);
    }
    void annotationGesturesCancelBeforeToolSwitchUndoOrClose() {
        auto *editor = new Editor(whiteImage(), {}, "en-US"); editor->show(); editor->activateWindow();
        auto *canvas = editor->findChild<Canvas *>();
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        auto *rectangle = editor->findChild<QAction *>("annotationTool-0");
        QVERIFY(canvas); QVERIFY(select); QVERIFY(rectangle);
        drawAnnotation(canvas, {20, 20}, {80, 60});
        const auto original = canvas->document.annotations();
        select->trigger();
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {70, 80}));
        rectangle->trigger();
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {70, 80}));
        QCOMPARE(canvas->document.annotations(), original); QVERIFY(!canvas->selectionMode());
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {140, 20}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {220, 90}));
        select->trigger();
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {220, 90}));
        QCOMPARE(canvas->document.annotations(), original);
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {90, 80}));
        QTRY_VERIFY(editor->isActiveWindow()); QTest::keyClick(canvas, Qt::Key_Escape);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {90, 80}));
        QVERIFY(editor->isVisible()); QCOMPARE(canvas->document.annotations(), original);
        rectangle->trigger();
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {140, 20}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {220, 90}));
        QTRY_VERIFY(editor->isActiveWindow()); QTest::keyClick(canvas, Qt::Key_Escape);
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {220, 90}));
        QVERIFY(editor->isVisible()); QCOMPARE(canvas->document.annotations(), original);
        select->trigger();
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {90, 80}));
        canvas->undo();
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {90, 80}));
        QCOMPARE(canvas->document.annotations(), original); QVERIFY(canvas->document.canUndo());
        QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 40}));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QCOMPARE(canvas->selectedAnnotation(), -1); QVERIFY(editor->isVisible());
        QPointer<Editor> window = editor;
        QTest::keyClick(canvas, Qt::Key_Escape); QTRY_VERIFY(!window || !window->isVisible());
    }
    void annotationMovePreviewHasNoGhostAndCopyExcludesSelection() {
        auto *editor = new Editor(whiteImage(), {}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *select = editor->findChild<QAction *>("selectAnnotation");
        QAction *copyAction = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("复制图片")) copyAction = action;
        QVERIFY(canvas); QVERIFY(select); QVERIFY(copyAction);
        canvas->setColor(Qt::red); drawAnnotation(canvas, {20, 30}, {80, 70});
        canvas->setColor(Qt::blue); drawAnnotation(canvas, {150, 35}, {220, 110});
        const auto original = canvas->document.annotations();
        const auto before = canvas->rendered();
        select->trigger();
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 50}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {120, 70}));
        QCOMPARE(canvas->document.annotations(), original); QCOMPARE(canvas->rendered(), before);
        const QImage preview = canvas->grab().toImage();
        const auto previewColor = [&](const QPointF &point) { return preview.pixelColor((QPointF(canvasPoint(canvas, point)) * preview.devicePixelRatio()).toPoint()); };
        QCOMPARE(previewColor({20, 50}), QColor(Qt::white));
        QCOMPARE(previewColor({150, 50}), QColor(Qt::blue));
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {120, 70}));
        QCOMPARE(canvas->selectedAnnotation(), 0);
        QSignalSpy copied(editor, &Editor::imageCopied); copyAction->trigger();
        QCOMPARE(copied.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(copied.first().first()), canvas->document.render());
        QCOMPARE(QApplication::clipboard()->image(), canvas->document.render());
        QCOMPARE(canvas->selectedAnnotation(), 0);
    }
    void cropSelectionUsesCurrentPixelsWithoutChangingHistory() {
        Canvas canvas(whiteImage()); canvas.resize(632, 332); canvas.show();
        drawAnnotation(&canvas, {20, 20}, {80, 60}); canvas.undo();
        const auto revision = canvas.document.revision();
        canvas.setCropMode(true);
        drawAnnotation(&canvas, {250.5, 120.5}, {10.5, 20.5});
        QCOMPARE(canvas.cropSelection(), QRect(10, 20, 241, 101));
        QCOMPARE(canvas.document.size(), QSize(300, 150)); QCOMPARE(canvas.rendered(), whiteImage());
        QCOMPARE(canvas.document.revision(), revision); QVERIFY(canvas.document.canRedo());
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.cropMode()); QVERIFY(canvas.cropSelection().isEmpty());
        canvas.setCropMode(true);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {250.5, 120.5}));
        const auto outside = canvas.imageRect().topLeft().toPoint() - QPoint(40, 40);
        QTest::mouseMove(&canvas, outside); QTest::mouseRelease(&canvas, Qt::LeftButton, {}, outside);
        QCOMPARE(canvas.cropSelection(), QRect(0, 0, 251, 121));
        canvas.setTool(Tool::Rectangle);
        QVERIFY(!canvas.cropMode()); QVERIFY(canvas.cropSelection().isEmpty());
        canvas.setCropMode(true); drawAnnotation(&canvas, {0, 0}, {300, 150});
        QCOMPARE(canvas.cropSelection(), QRect(0, 0, 300, 150)); QVERIFY(!canvas.applyCrop());
        canvas.setCropMode(true); QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {30, 30}));
        QVERIFY(canvas.cropSelection().isEmpty()); QVERIFY(!canvas.applyCrop());
        QCOMPARE(canvas.document.revision(), revision); QVERIFY(canvas.document.canRedo());
        canvas.redo(); QCOMPARE(canvas.document.annotations().size(), 1);
    }
    void editorCropShortcutButtonAndEnterCommitOneEdit() {
        QTemporaryDir dir;
        auto *editor = new Editor(whiteImage(), {{"dir", dir.path()}, {"nameTemplate", "crop"}}, "en-US");
        editor->show(); editor->activateWindow();
        auto *canvas = editor->findChild<Canvas *>();
        auto *toolbar = editor->findChild<QToolBar *>();
        auto *crop = editor->findChild<QAction *>("cropImage");
        auto *menu = editor->findChild<QMenu *>("ImageTransformMenu");
        auto *quickSave = editor->findChild<QAction *>("quickSave");
        QVERIFY(canvas); QVERIFY(toolbar); QVERIFY(crop); QVERIFY(menu); QVERIFY(quickSave);
        auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(crop)); QVERIFY(button);
        QCOMPARE(button->popupMode(), QToolButton::MenuButtonPopup); QCOMPARE(button->menu(), menu);
        QVERIFY(editor->findChild<QAction *>("flipHorizontal")); QVERIFY(editor->findChild<QAction *>("flipVertical"));
        drawAnnotation(canvas, {20, 20}, {100, 100});
        const auto before = canvas->rendered();
        QTRY_VERIFY(editor->isActiveWindow()); QTest::keyClick(canvas, Qt::Key_C);
        QVERIFY(canvas->cropMode()); QVERIFY(crop->isChecked());
        drawAnnotation(canvas, {25, 20}, {230, 120}); const auto roi = canvas->cropSelection();
        QVERIFY(!roi.isEmpty()); QCOMPARE(canvas->rendered(), before);
        QAction *copy = nullptr; QAction *pin = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) {
            if (action->text() == QStringLiteral("复制图片")) copy = action;
            if (action->text() == QStringLiteral("贴图")) pin = action;
        }
        QVERIFY(copy); QVERIFY(pin);
        copy->trigger(); QCOMPARE(QApplication::clipboard()->image(), before);
        quickSave->trigger(); QCOMPARE(QImage(dir.filePath("crop.png")), before);
        QTest::keyClick(canvas, Qt::Key_Return);
        const auto cropped = before.copy(roi);
        QCOMPARE(canvas->rendered(), cropped); QCOMPARE(canvas->document.size(), roi.size()); QVERIFY(!canvas->cropMode());
        copy->trigger(); QCOMPARE(QApplication::clipboard()->image(), cropped);
        quickSave->trigger(); QCOMPARE(QImage(dir.filePath("crop-2.png")), cropped);
        canvas->undo(); QCOMPARE(canvas->rendered(), before); canvas->redo(); QCOMPARE(canvas->rendered(), cropped);
        QTest::mouseClick(button, Qt::LeftButton); QVERIFY(canvas->cropMode());
        drawAnnotation(canvas, {10, 10}, {100, 60}); const auto secondRoi = canvas->cropSelection();
        QTest::mouseClick(button, Qt::LeftButton);
        const auto twiceCropped = cropped.copy(secondRoi); QCOMPARE(canvas->rendered(), twiceCropped);
        canvas->undo(); QCOMPARE(canvas->rendered(), cropped);
        canvas->undo(); QCOMPARE(canvas->rendered(), before);
        canvas->undo(); QCOMPARE(canvas->rendered(), whiteImage());
        canvas->redo(); canvas->redo(); canvas->redo(); QCOMPARE(canvas->rendered(), twiceCropped);
        QSignalSpy pinned(editor, &Editor::pinRequested); pin->trigger();
        QCOMPARE(pinned.count(), 1); QCOMPARE(qvariant_cast<QImage>(pinned.first().first()), twiceCropped);
    }
    void cropEscapeAndDrawingToolDiscardOnlyThePreview() {
        auto *editor = new Editor(whiteImage(), {}, "en-US"); editor->show(); editor->activateWindow();
        auto *canvas = editor->findChild<Canvas *>();
        auto *crop = editor->findChild<QAction *>("cropImage");
        auto *rectangle = editor->findChild<QAction *>("annotationTool-0");
        QVERIFY(canvas); QVERIFY(crop); QVERIFY(rectangle);
        drawAnnotation(canvas, {20, 20}, {80, 60}); const auto before = canvas->rendered();
        const auto revision = canvas->document.revision();
        crop->trigger(); drawAnnotation(canvas, {30, 20}, {220, 120});
        QTRY_VERIFY(editor->isActiveWindow()); QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(editor->isVisible()); QVERIFY(!canvas->cropMode()); QVERIFY(canvas->cropSelection().isEmpty());
        QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.revision(), revision);
        crop->trigger(); drawAnnotation(canvas, {30, 20}, {220, 120}); rectangle->trigger();
        QVERIFY(!canvas->cropMode()); QVERIFY(canvas->cropSelection().isEmpty()); QVERIFY(rectangle->isChecked());
        QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.revision(), revision);
    }
    void editorFlipMenuTransformsPixelsAndAnnotationsTogether() {
        QImage image = whiteImage(); QPainter painter(&image);
        painter.fillRect(QRect(0, 0, 120, 70), QColor("#1261a0"));
        painter.fillRect(QRect(230, 90, 70, 60), QColor("#ffce31")); painter.end();
        auto *editor = new Editor(image, {}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *horizontal = editor->findChild<QAction *>("flipHorizontal");
        auto *vertical = editor->findChild<QAction *>("flipVertical");
        QVERIFY(canvas); QVERIFY(horizontal); QVERIFY(vertical);
        canvas->setTool(Tool::Arrow); drawAnnotation(canvas, {40, 30}, {210, 120});
        const auto annotations = canvas->document.annotations(); const auto before = canvas->rendered();
        canvas->setSelectionMode(true); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {40, 30}));
        QCOMPARE(canvas->selectedAnnotation(), 0);
        canvas->setCropMode(true); drawAnnotation(canvas, {20, 20}, {220, 120});
        horizontal->trigger();
        QCOMPARE(canvas->rendered(), before.mirrored(true, false)); QCOMPARE(canvas->document.annotations(), annotations);
        QCOMPARE(canvas->selectedAnnotation(), -1); QVERIFY(!canvas->cropMode()); QVERIFY(canvas->cropSelection().isEmpty());
        canvas->undo(); QCOMPARE(canvas->rendered(), before); canvas->redo();
        vertical->trigger(); QCOMPARE(canvas->rendered(), before.mirrored(true, true));
        canvas->undo(); QCOMPARE(canvas->rendered(), before.mirrored(true, false));
        horizontal->trigger(); QCOMPARE(canvas->rendered(), before); QVERIFY(!canvas->document.canRedo());
        QCOMPARE(canvas->document.original(), image);
    }
    void editorImageEffectsUseTransformMenuAndExportTheResult_data() {
        QTest::addColumn<bool>("invert");
        QTest::newRow("grayscale") << false;
        QTest::newRow("invert") << true;
    }
    void editorImageEffectsUseTransformMenuAndExportTheResult() {
        QFETCH(bool, invert);
        QTemporaryDir dir;
        QImage image = whiteImage(); image.fill(QColor(80, 120, 200, 190));
        auto *editor = new Editor(image, {{"dir", dir.path()}, {"nameTemplate", "effect"}}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *menu = editor->findChild<QMenu *>("ImageTransformMenu");
        auto *effect = editor->findChild<QAction *>(invert ? "invertImage" : "grayscaleImage");
        auto *quickSave = editor->findChild<QAction *>("quickSave");
        QVERIFY(canvas); QVERIFY(menu); QVERIFY(effect); QVERIFY(quickSave);
        QStringList actions; int separators = 0;
        for (auto *action : menu->actions()) {
            if (action->isSeparator()) ++separators; else actions.append(action->objectName());
        }
        QCOMPARE(actions, QStringList({"flipHorizontal", "flipVertical", "grayscaleImage", "invertImage", "brightnessImage"}));
        QCOMPARE(separators, 1); QVERIFY(menu->actions()[2]->isSeparator()); QVERIFY(effect->shortcut().isEmpty());
        canvas->setColor(Qt::red); drawAnnotation(canvas, {20, 20}, {100, 100});
        const auto annotations = canvas->document.annotations(); const auto before = canvas->rendered();
        QTest::mousePress(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {140, 20}));
        QTest::mouseMove(canvas, canvasPoint(canvas, {220, 80})); effect->trigger();
        QTest::mouseRelease(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {220, 80}));
        QCOMPARE(canvas->document.annotations(), annotations); QCOMPARE(canvas->document.original(), image);
        const auto filtered = canvas->rendered();
        for (const auto &point : {QPoint(60, 20), QPoint(260, 130)}) {
            const QColor source = before.pixelColor(point);
            const int gray = qGray(source.rgb());
            const QColor expected = invert ? QColor(255 - source.red(), 255 - source.green(), 255 - source.blue(), source.alpha())
                                           : QColor(gray, gray, gray, source.alpha());
            QCOMPARE(filtered.pixelColor(point), expected);
        }
        QAction *copy = nullptr; QAction *pin = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) {
            if (action->text() == QStringLiteral("复制图片")) copy = action;
            if (action->text() == QStringLiteral("贴图")) pin = action;
        }
        QVERIFY(copy); QVERIFY(pin);
        copy->trigger(); QCOMPARE(QApplication::clipboard()->image(), filtered);
        quickSave->trigger(); QCOMPARE(QImage(dir.filePath("effect.png")), filtered);
        canvas->undo(); QCOMPARE(canvas->rendered(), before);
        canvas->redo(); QCOMPARE(canvas->rendered(), filtered); canvas->undo();
        canvas->setCropMode(true); drawAnnotation(canvas, {10, 10}, {240, 120});
        const auto roi = canvas->cropSelection(); QVERIFY(canvas->applyCrop()); canvas->flip(Qt::Horizontal);
        QCOMPARE(canvas->rendered(), before.copy(roi).mirrored(true, false)); QVERIFY(!canvas->document.canRedo());
        const auto croppedSize = canvas->document.size();
        canvas->setCropMode(true); drawAnnotation(canvas, {10, 10}, {100, 60}); effect->trigger();
        QVERIFY(!canvas->cropMode()); QVERIFY(canvas->cropSelection().isEmpty()); QCOMPARE(canvas->document.size(), croppedSize);
        const auto final = canvas->rendered(); QSignalSpy pinned(editor, &Editor::pinRequested); pin->trigger();
        QCOMPARE(pinned.count(), 1); QCOMPARE(qvariant_cast<QImage>(pinned.first().first()), final);
    }
    void imageEffectsKeepOldAnnotationsEditableAndNewInkColored() {
        Canvas canvas(whiteImage()); canvas.resize(632, 332); canvas.show();
        canvas.setColor(Qt::red); drawAnnotation(&canvas, {20, 20}, {100, 90});
        canvas.applyEffect(ImageEffect::Grayscale); canvas.setSelectionMode(true);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {20, 50}));
        QCOMPARE(canvas.selectedAnnotation(), 0);
        canvas.setColor(Qt::blue); canvas.setLineWidth(5); drawAnnotation(&canvas, {20, 50}, {30, 55});
        const auto edited = canvas.document.annotations()[0];
        QCOMPARE(edited.points, QVector<QPointF>({{30, 25}, {110, 95}}));
        QCOMPARE(edited.color, QColor(Qt::blue)); QCOMPARE(edited.width, qreal(5));
        const int gray = qGray(QColor(Qt::blue).rgb());
        QCOMPARE(canvas.rendered().pixelColor(30, 60), QColor(gray, gray, gray));
        canvas.setTool(Tool::Rectangle); canvas.setColor(Qt::red); canvas.setLineWidth(3);
        drawAnnotation(&canvas, {160, 20}, {220, 80});
        QCOMPARE(canvas.rendered().pixelColor(190, 20), QColor(Qt::red));
        const auto beforeInvert = canvas.rendered();
        canvas.setSelectionMode(true);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {30, 60}));
        QTest::mouseMove(&canvas, canvasPoint(&canvas, {60, 70})); canvas.applyEffect(ImageEffect::Invert);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {60, 70}));
        QCOMPARE(canvas.selectedAnnotation(), -1); QCOMPARE(canvas.document.annotations()[0], edited);
        QImage inverted = beforeInvert; inverted.invertPixels(QImage::InvertRgb); QCOMPARE(canvas.rendered(), inverted);
        canvas.setTool(Tool::Rectangle); canvas.setColor(Qt::green); drawAnnotation(&canvas, {160, 105}, {220, 140});
        QCOMPARE(canvas.rendered().pixelColor(190, 105), QColor(Qt::green));
        canvas.undo(); QCOMPARE(canvas.rendered(), inverted);
        canvas.undo(); QCOMPARE(canvas.rendered(), beforeInvert);
        canvas.redo(); QCOMPARE(canvas.rendered(), inverted);
    }
    void brightnessPreviewCancelAndZeroPreserveSelectionCropAndRedo() {
        QImage image = whiteImage(); image.fill(QColor(70, 100, 130));
        auto *editor = new Editor(image, {}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *brightness = editor->findChild<QAction *>("brightnessImage");
        QVERIFY(canvas); QVERIFY(brightness); QVERIFY(brightness->shortcut().isEmpty());
        drawAnnotation(canvas, {20, 20}, {100, 100}); drawAnnotation(canvas, {160, 20}, {220, 100}); canvas->undo();
        const auto before = canvas->rendered(); const auto annotations = canvas->document.annotations();
        const auto revision = canvas->document.revision(); QVERIFY(canvas->document.canRedo());
        canvas->setSelectionMode(true); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 50}));
        QCOMPARE(canvas->selectedAnnotation(), 0); QSignalSpy changed(canvas, &Canvas::changed);
        auto displayedColor = [&] {
            const QImage displayed = canvas->grab().toImage();
            return displayed.pixelColor((QPointF(canvasPoint(canvas, {260, 100})) * displayed.devicePixelRatio()).toPoint());
        };
        auto openDialog = [&](bool acceptZero) {
            const auto selected = canvas->selectedAnnotation(); const auto roi = canvas->cropSelection();
            const bool selecting = canvas->selectionMode(), cropping = canvas->cropMode();
            bool opened = false; QTimer answer;
            connect(&answer, &QTimer::timeout, this, [&] {
                auto *dialog = editor->findChild<QInputDialog *>("BrightnessDialog"); if (!dialog || !dialog->isVisible()) return;
                answer.stop(); opened = true;
                const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
                QCOMPARE(dialog->windowTitle(), QStringLiteral("调整亮度"));
                QCOMPARE(dialog->labelText(), QStringLiteral("负值变暗，正值变亮（-100 至 100）："));
                QCOMPARE(dialog->inputMode(), QInputDialog::IntInput); QCOMPARE(dialog->intValue(), 0);
                QCOMPARE(dialog->intMinimum(), -100); QCOMPARE(dialog->intMaximum(), 100); QCOMPARE(dialog->intStep(), 1);
                QCOMPARE(dialog->okButtonText(), QStringLiteral("应用")); QCOMPARE(dialog->cancelButtonText(), QStringLiteral("取消"));
                dialog->setIntValue(25); QCOMPARE(displayedColor(), QColor(134, 164, 194));
                dialog->setIntValue(-20); QCOMPARE(displayedColor(), QColor(19, 49, 79));
                QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.annotations(), annotations);
                QCOMPARE(canvas->document.revision(), revision); QCOMPARE(changed.count(), 0);
                QCOMPARE(canvas->selectedAnnotation(), selected); QCOMPARE(canvas->cropSelection(), roi);
                if (acceptZero) { dialog->setIntValue(0); dialog->accept(); } else dialog->reject();
            });
            answer.start(10); brightness->trigger(); QVERIFY(opened);
            QCOMPARE(canvas->rendered(), before); QCOMPARE(displayedColor(), QColor(70, 100, 130));
            QCOMPARE(canvas->selectedAnnotation(), selected); QCOMPARE(canvas->cropSelection(), roi);
            QCOMPARE(canvas->selectionMode(), selecting); QCOMPARE(canvas->cropMode(), cropping);
            QCOMPARE(canvas->document.revision(), revision); QVERIFY(canvas->document.canRedo()); QCOMPARE(changed.count(), 0);
        };
        openDialog(false);
        canvas->setCropMode(true); drawAnnotation(canvas, {140, 20}, {280, 130}); QVERIFY(!canvas->cropSelection().isEmpty());
        openDialog(true);
        canvas->redo(); QCOMPARE(canvas->document.annotations().size(), 2);
    }
    void brightnessDialogCommitsOnceAndExportsNewColoredInk() {
        QTemporaryDir dir; QImage image = whiteImage(); image.fill(QColor(80, 120, 200, 190));
        auto *editor = new Editor(image, {{"dir", dir.path()}, {"nameTemplate", "brightness"}}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        auto *brightness = editor->findChild<QAction *>("brightnessImage");
        auto *quickSave = editor->findChild<QAction *>("quickSave");
        QVERIFY(canvas); QVERIFY(brightness); QVERIFY(quickSave);
        canvas->setColor(Qt::red); drawAnnotation(canvas, {20, 20}, {100, 100});
        const auto before = canvas->rendered(); const auto annotations = canvas->document.annotations();
        const auto revision = canvas->document.revision();
        canvas->setSelectionMode(true); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 50}));
        QSignalSpy changed(canvas, &Canvas::changed); bool opened = false; QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            auto *dialog = editor->findChild<QInputDialog *>("BrightnessDialog"); if (!dialog || !dialog->isVisible()) return;
            answer.stop(); opened = true;
            const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
            dialog->setIntValue(20); dialog->setIntValue(40); dialog->setIntValue(20);
            QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.revision(), revision); QCOMPARE(changed.count(), 0);
            dialog->accept();
        });
        answer.start(10); brightness->trigger(); QVERIFY(opened);
        const auto filtered = canvas->rendered();
        QCOMPARE(filtered.pixelColor(260, 130), QColor(131, 171, 251, 190));
        QCOMPARE(filtered.pixelColor(60, 20), QColor(255, 51, 51));
        QCOMPARE(canvas->document.annotations(), annotations); QCOMPARE(canvas->document.original(), image);
        QCOMPARE(canvas->document.revision(), revision + 1); QCOMPARE(changed.count(), 1); QCOMPARE(canvas->selectedAnnotation(), -1);
        canvas->undo(); QCOMPARE(canvas->rendered(), before); canvas->redo(); QCOMPARE(canvas->rendered(), filtered);
        canvas->setTool(Tool::Rectangle); canvas->setColor(Qt::green); drawAnnotation(canvas, {160, 105}, {220, 140});
        const auto final = canvas->rendered(); QCOMPARE(final.pixelColor(190, 105), QColor(Qt::green));
        QAction *copy = nullptr; QAction *pin = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) {
            if (action->text() == QStringLiteral("复制图片")) copy = action;
            if (action->text() == QStringLiteral("贴图")) pin = action;
        }
        QVERIFY(copy); QVERIFY(pin);
        copy->trigger(); QCOMPARE(QApplication::clipboard()->image(), final);
        quickSave->trigger(); QCOMPARE(QImage(dir.filePath("brightness.png")), final);
        QSignalSpy pinned(editor, &Editor::pinRequested); pin->trigger();
        QCOMPARE(pinned.count(), 1); QCOMPARE(qvariant_cast<QImage>(pinned.first().first()), final);
    }
    void croppedAndFlippedTextKeepsItsFrameAndNewTextReadsNormally() {
        Canvas canvas(whiteImage()); canvas.resize(632, 332); canvas.show();
        auto addText = [&](const QPointF &point, const QString &text) {
            bool opened = false; QTimer answer;
            connect(&answer, &QTimer::timeout, this, [&] {
                auto *dialog = canvas.findChild<QInputDialog *>(); if (!dialog || !dialog->isVisible()) return;
                answer.stop(); opened = true;
                const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
                QVERIFY(dialog->textValue().isEmpty()); dialog->setTextValue(text); dialog->accept();
            });
            canvas.setTool(Tool::Text); answer.start(10);
            QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, point)); QVERIFY(opened);
        };
        addText({70, 35}, QStringLiteral("wide line\nshort"));
        QCOMPARE(canvas.document.annotations().size(), 1); const auto original = canvas.document.annotations()[0];
        const auto before = canvas.rendered();
        canvas.setCropMode(true); drawAnnotation(&canvas, {50, 20}, {250, 120});
        QCOMPARE(canvas.cropSelection(), QRect(50, 20, 200, 100)); QVERIFY(canvas.applyCrop());
        QCOMPARE(canvas.rendered(), before.copy(50, 20, 200, 100)); canvas.flip(Qt::Horizontal);
        QCOMPARE(canvas.rendered(), before.copy(50, 20, 200, 100).mirrored(true, false));
        QCOMPARE(canvas.document.annotations()[0], original);
        canvas.setSelectionMode(true);
        const auto point = canvas.document.bounds(0).center();
        const auto transformed = canvas.rendered(); const auto revision = canvas.document.revision();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, point));
        QCOMPARE(canvas.rendered(), transformed); QCOMPARE(canvas.document.revision(), revision);
        drawAnnotation(&canvas, point, point + QPointF(12, 6));
        auto moved = original; for (auto &p : moved.points) p += QPointF(-12, 6);
        QCOMPARE(canvas.document.annotations()[0], moved);
        canvas.setColor(Qt::blue); canvas.setLineWidth(4); moved.color = Qt::blue; moved.width = 4;
        QImage expected = whiteImage(); paintAnnotation(expected, moved);
        expected = expected.copy(50, 20, 200, 100).mirrored(true, false);
        QCOMPARE(canvas.rendered(), expected);
        canvas.setTool(Tool::Text); canvas.setColor(Qt::red); canvas.setLineWidth(3);
        addText({20, 20}, QStringLiteral("new ->"));
        const Annotation added{Tool::Text, {{20, 20}}, Qt::red, 3, QStringLiteral("new ->")};
        QCOMPARE(canvas.document.annotations().size(), 2); QCOMPARE(canvas.document.annotations()[1], added);
        paintAnnotation(expected, added); QCOMPARE(canvas.rendered(), expected);
        canvas.undo(); QCOMPARE(canvas.document.annotations().size(), 1);
        canvas.redo(); QCOMPARE(canvas.rendered(), expected);
    }
    void partiallyCroppedAnnotationDoesNotJumpAndMovesAsOneShape() {
        Canvas canvas(whiteImage()); canvas.resize(632, 332); canvas.show();
        drawAnnotation(&canvas, {20, 20}, {200, 100}); const auto original = canvas.document.annotations()[0];
        canvas.setCropMode(true); drawAnnotation(&canvas, {100, 10}, {250, 140}); QVERIFY(canvas.applyCrop());
        canvas.flip(Qt::Horizontal); canvas.resize(332, 292); canvas.setSelectionMode(true);
        QCOMPARE(canvas.imageRect().size(), QSizeF(300, 260));
        const auto before = canvas.rendered(); const auto revision = canvas.document.revision();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {50, 50}));
        QCOMPARE(canvas.selectedAnnotation(), 0); QCOMPARE(canvas.document.annotations()[0], original);
        QCOMPARE(canvas.rendered(), before); QCOMPARE(canvas.document.revision(), revision);
        drawAnnotation(&canvas, {50, 50}, {30, 70});
        auto moved = original; for (auto &point : moved.points) point += QPointF(20, 20);
        QCOMPARE(canvas.document.annotations()[0], moved);
        const auto after = canvas.rendered(); canvas.undo(); QCOMPARE(canvas.rendered(), before);
        canvas.redo(); QCOMPARE(canvas.rendered(), after);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, canvasPoint(&canvas, {30, 60}));
        QCOMPARE(canvas.selectedAnnotation(), 0);
        for (int i = 0; i < 5; ++i) QTest::keyClick(&canvas, Qt::Key_Up, Qt::ShiftModifier);
        for (auto &point : moved.points) point += QPointF(0, -30);
        QCOMPARE(canvas.document.annotations()[0], moved);
        QCOMPARE(canvas.document.bounds(0).top(), qreal(0));
    }
    void editingPinApplyExportsCropFlipAndEffectResult() {
        auto *editor = new Editor(whiteImage(), {}, "en-US", true); editor->show();
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        drawAnnotation(canvas, {20, 20}, {100, 100}); const auto before = canvas->rendered();
        canvas->flip(Qt::Vertical); canvas->setCropMode(true); drawAnnotation(canvas, {20, 10}, {240, 120});
        const auto roi = canvas->cropSelection(); QVERIFY(canvas->applyCrop());
        QImage expected = before.mirrored(false, true).copy(roi); expected.invertPixels(QImage::InvertRgb);
        canvas->applyEffect(ImageEffect::Invert); QCOMPARE(canvas->rendered(), expected);
        canvas->applyEffect(ImageEffect::Brightness, 20); QVERIFY(canvas->rendered() != expected); expected = canvas->rendered();
        QAction *apply = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("应用并关闭")) apply = action;
        QVERIFY(apply); QSignalSpy applied(editor, &Editor::applyRequested); apply->trigger();
        QCOMPARE(applied.count(), 1); QCOMPARE(qvariant_cast<QImage>(applied.first().first()), expected);
    }
    void editorTransformsInvalidateBlockedOcr_data() {
        QTest::addColumn<QString>("operation");
        QTest::newRow("crop") << QString("cropImage");
        QTest::newRow("flip") << QString("flipHorizontal");
        QTest::newRow("grayscale") << QString("grayscaleImage");
        QTest::newRow("invert") << QString("invertImage");
        QTest::newRow("brightness") << QString("brightnessImage");
    }
    void editorTransformsInvalidateBlockedOcr() {
        QFETCH(QString, operation);
        auto *editor = new Editor(whiteImage(420), {}, "en-US"); editor->show();
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        drawAnnotation(canvas, {20, 20}, {100, 100});
        QAction *recognize = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("复制文字")) recognize = action;
        QVERIFY(recognize);
        ocrBlocked = true; const auto unblock = qScopeGuard([] { ocrBlocked = false; }); const auto started = ocrStarted.load();
        recognize->trigger(); QTRY_COMPARE(ocrStarted.load(), started + 1);
        QPointer<OcrJob> stale = editor->findChild<OcrJob *>(); QVERIFY(stale); QSignalSpy staleFinished(stale, &OcrJob::finished);
        const auto revision = canvas->document.revision();
        canvas->setCropMode(true); drawAnnotation(canvas, {30, 20}, {330, 120});
        QCOMPARE(canvas->document.revision(), revision); QVERIFY(stale); QCOMPARE(staleFinished.count(), 0);
        canvas->setSelectionMode(true); QTest::mouseClick(canvas, Qt::LeftButton, {}, canvasPoint(canvas, {20, 50}));
        QCOMPARE(canvas->selectedAnnotation(), 0);
        canvas->setColor(canvas->color); canvas->setLineWidth(canvas->lineWidth);
        QCOMPARE(canvas->document.revision(), revision); QVERIFY(stale);
        if (operation == "cropImage") {
            canvas->setCropMode(true); drawAnnotation(canvas, {30, 20}, {330, 120}); QVERIFY(canvas->applyCrop());
        } else if (operation == "brightnessImage") {
            auto *brightness = editor->findChild<QAction *>(operation); QVERIFY(brightness);
            bool opened = false; int phase = 0; QTimer answer;
            connect(&answer, &QTimer::timeout, this, [&] {
                auto *dialog = editor->findChild<QInputDialog *>("BrightnessDialog"); if (!dialog || !dialog->isVisible()) return;
                answer.stop(); opened = true;
                const auto dismiss = qScopeGuard([=] { if (dialog->isVisible()) dialog->reject(); });
                dialog->setIntValue(phase == 1 ? 0 : 20); QVERIFY(stale); QCOMPARE(staleFinished.count(), 0);
                QCOMPARE(canvas->document.revision(), revision);
                if (phase == 0) dialog->reject(); else dialog->accept();
            });
            for (; phase < 3; ++phase) {
                opened = false; answer.start(10); brightness->trigger(); QVERIFY(opened);
                if (phase < 2) { QVERIFY(stale); QCOMPARE(canvas->document.revision(), revision); }
            }
        } else {
            auto *action = editor->findChild<QAction *>(operation); QVERIFY(action); action->trigger();
        }
        QVERIFY(!stale); QVERIFY(QApplication::clipboard()->text().isEmpty());
        QVERIFY(editor->findChildren<OcrResultDialog *>().isEmpty());
        ocrBlocked = false; recognize->trigger();
        QTRY_COMPARE(QApplication::clipboard()->text(), operation == "cropImage" ? QString("Hello Qt\nNext") : QString("  Wide  \tQt\r\nNext!  "));
        QCOMPARE(staleFinished.count(), 0);
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
    void editorOcrCopiesCompleteText() {
        auto *editor = new Editor(whiteImage(420), {}, "en-US");
        QAction *recognize = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("复制文字")) recognize = action;
        QVERIFY(recognize);
        recognize->trigger();
        QTRY_COMPARE(QApplication::clipboard()->text(), QString("  Wide  \tQt\r\nNext!  "));
        auto *dialog = editor->findChild<OcrResultDialog *>(); QVERIFY(dialog); QVERIFY(dialog->isVisible());
        auto *edit = dialog->findChild<QPlainTextEdit *>("OcrEditedText"); QVERIFY(edit);
        edit->selectAll(); edit->insertPlainText(QStringLiteral("校对后的文字"));
        dialog->findChild<QPushButton *>("OcrCopyText")->click();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("校对后的文字"));
        dialog->findChild<QTabWidget *>("OcrResultTabs")->setCurrentIndex(1);
        dialog->findChild<QPushButton *>("OcrCopyText")->click();
        QCOMPARE(QApplication::clipboard()->text(), QString("  Wide  \tQt\r\nNext!  "));
    }
    void pinProofreadingUsesSnapshotWithoutChangingGeometryOrRecognizingAgain() {
        auto *window = pin(whiteImage(420)); QTRY_VERIFY(!window->toolTip().isEmpty());
        const int started = ocrStarted.load(); const QString original = "  Wide  \tQt\r\nNext!  ";
        bool opened = false; QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *menu = window->findChild<QMenu *>("PinMenu"); if (!menu || !menu->isVisible()) return;
            timer.stop(); const auto close = qScopeGuard([&] { menu->close(); });
            auto *action = menu->findChild<QAction *>("ProofreadOcrText"); QVERIFY(action); QVERIFY(action->isEnabled());
            QCOMPARE(menu->actions().size(), 6); action->trigger(); opened = true;
        });
        timer.start(10);
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), window->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(window, &event); QVERIFY(opened);
        auto *dialog = window->findChild<OcrResultDialog *>(); QVERIFY(dialog); QCOMPARE(ocrStarted.load(), started);
        auto *edit = dialog->findChild<QPlainTextEdit *>("OcrEditedText"); QVERIFY(edit);
        edit->selectAll(); edit->insertPlainText("Independent correction");
        dialog->findChild<QPushButton *>("OcrCopyText")->click(); QCOMPARE(QApplication::clipboard()->text(), QString("Independent correction"));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier); copy(window); QCOMPARE(QApplication::clipboard()->text(), original);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(window, QPoint(80, 20)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(80, 20));
        copy(window); QCOMPARE(QApplication::clipboard()->text(), QString("Wide  \tQt"));
        window->setImage(whiteImage(200)); QTRY_COMPARE(ocrStarted.load(), started + 1);
        QTRY_VERIFY(window->findChildren<OcrJob *>().isEmpty());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier); copy(window); QCOMPARE(QApplication::clipboard()->text(), QString("Old"));
        QCOMPARE(window->findChildren<OcrResultDialog *>().size(), 1);
        QCOMPARE(edit->toPlainText(), QString("Independent correction"));
        dialog->findChild<QTabWidget *>("OcrResultTabs")->setCurrentIndex(1);
        dialog->findChild<QPushButton *>("OcrCopyText")->click(); QCOMPARE(QApplication::clipboard()->text(), original);
    }
    void editorCopyAsFileExportsAnnotationsAndSurvivesWindowClose() {
        auto *editor = new Editor(whiteImage(), {{"copyAsFile", true}}, "en-US");
        editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        canvas->document.add({Tool::Rectangle, {{20, 20}, {100, 100}}, Qt::red, 3, {}});
        const auto expected = canvas->rendered();
        QSignalSpy copied(editor, &Editor::imageCopied);
        QAction *copyAction = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == QStringLiteral("复制图片")) copyAction = action;
        QVERIFY(copyAction); copyAction->trigger(); QCOMPARE(copied.count(), 1);
        const auto urls = QApplication::clipboard()->mimeData()->urls(); QCOMPARE(urls.size(), 1);
        const QString path = urls.first().toLocalFile();
        const auto cleanup = qScopeGuard([&] { QApplication::clipboard()->clear(); QDir(QFileInfo(path).absolutePath()).removeRecursively(); });
        editor->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(QImage(path), expected);
    }
    void pinFileCopyDoesNotChangeSelectedTextCopy() {
        auto *window = pin();
        window->configureSave({{"copyAsFile", true}, {"format", "jpeg"}});
        QTRY_VERIFY(!window->toolTip().isEmpty());
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier); copy(window);
        QCOMPARE(QApplication::clipboard()->text(), QString("Hello Qt\nNext"));
        QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(250, 120));
        copy(window);
        const auto urls = QApplication::clipboard()->mimeData()->urls(); QCOMPARE(urls.size(), 1);
        const QString path = urls.first().toLocalFile();
        const auto cleanup = qScopeGuard([&] { QApplication::clipboard()->clear(); QDir(QFileInfo(path).absolutePath()).removeRecursively(); });
        QCOMPARE(QImageReader::imageFormat(path), QByteArray("jpeg"));
        QCOMPARE(QImage(path).size(), window->image().size());
    }
    void quickSaveExportsRenderedImageWithoutDialog() {
        QTemporaryDir dir;
        auto *editor = new Editor(whiteImage(), {{"dir", dir.path()}, {"nameTemplate", "capture"}}, "en-US");
        editor->show(); editor->activateWindow(); QTest::qWait(30);
        auto *canvas = editor->findChild<Canvas *>();
        canvas->document.add({Tool::Rectangle, {{20, 20}, {100, 100}}, Qt::red, 3, {}});
        QSignalSpy saved(editor, &Editor::imageSaved);
        QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(QImage(dir.filePath("capture.png")), canvas->rendered());
        QVERIFY(!editor->findChild<QFileDialog *>());
        QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(saved.count(), 2); QCOMPARE(QImage(dir.filePath("capture-2.png")), canvas->rendered());
        editor->configureSave({{"dir", dir.path()}, {"nameTemplate", "updated"}, {"format", "jpeg"}});
        editor->findChild<QAction *>("quickSave")->trigger();
        QCOMPARE(QImageReader::imageFormat(dir.filePath("updated.jpg")), QByteArray("jpeg"));
    }
    void failedQuickSaveKeepsTheEditorAndAnnotations() {
        QTemporaryDir dir;
        QFile blocked(dir.filePath("blocked")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("original"); blocked.close();
        auto *editor = new Editor(whiteImage(), {{"dir", blocked.fileName()}}, "en-US");
        editor->show();
        auto *canvas = editor->findChild<Canvas *>();
        canvas->document.add({Tool::Rectangle, {{20, 20}, {100, 100}}, Qt::red, 3, {}});
        const auto before = canvas->rendered();
        QSignalSpy saved(editor, &Editor::imageSaved);
        editor->findChild<QAction *>("quickSave")->trigger();
        QCOMPARE(saved.count(), 0); QVERIFY(editor->isVisible()); QVERIFY(canvas->document.canUndo());
        QCOMPARE(canvas->rendered(), before); QVERIFY(editor->statusBar()->currentMessage().contains(QStringLiteral("保存失败")));
        QVERIFY(blocked.open(QIODevice::ReadOnly)); QCOMPARE(blocked.readAll(), QByteArray("original"));
    }
    void pinQuickSaveUsesShortcutAndReportsSuccess() {
        QTemporaryDir dir;
        auto *window = pin();
        window->configureSave({{"dir", dir.path()}, {"nameTemplate", "pin"}});
        QSignalSpy saved(window, &Pin::imageSaved);
        QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(saved.count(), 1); QCOMPARE(QImage(dir.filePath("pin.png")), window->image());
        QVERIFY(!window->findChild<QFileDialog *>()); QVERIFY(window->isVisible());
    }
    void normalSaveKeepsDialogAndConfiguredFormat() {
        QTemporaryDir dir;
        auto *editor = new Editor(whiteImage(), {{"dir", dir.path()}, {"nameTemplate", "dialog"}, {"format", "jpeg"}}, "en-US");
        editor->show(); editor->activateWindow(); QTest::qWait(30);
        const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard([=] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); });
        bool opened = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *dialog = editor->findChild<QFileDialog *>(); dialog && dialog->isVisible()) {
                opened = true;
                dismiss.stop();
                const auto closeDialog = qScopeGuard([=] { dialog->reject(); });
                QCOMPARE(dialog->defaultSuffix(), QString("jpg"));
                QCOMPARE(dialog->selectedFiles().first(), dir.filePath("dialog.jpg"));
                QMetaObject::invokeMethod(dialog, "filterSelected", Q_ARG(QString, QStringLiteral("PNG 图像 (*.png)")));
                QCOMPARE(dialog->defaultSuffix(), QString("png"));
                QCOMPARE(dialog->selectedFiles().first(), dir.filePath("dialog.png"));
                auto *name = dialog->findChild<QLineEdit *>("fileNameEdit"); QVERIFY(name);
                name->clear();
                QMetaObject::invokeMethod(dialog, "filterSelected", Q_ARG(QString, QStringLiteral("JPEG 图像 (*.jpg *.jpeg)")));
                QVERIFY(name->text().isEmpty());
                name->setText("report.v2");
                QMetaObject::invokeMethod(dialog, "filterSelected", Q_ARG(QString, QStringLiteral("PNG 图像 (*.png)")));
                QCOMPARE(dialog->selectedFiles().first(), dir.filePath("report.v2.png"));
            }
        });
        dismiss.start(10);
        QSignalSpy saved(editor, &Editor::imageSaved);
        QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(opened); QCOMPARE(saved.count(), 0); QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }
    void captureQuickSaveShortcutKeepsSelectedImage() {
        auto *overlay = new Overlay({whiteImage(900), QRect(0, 0, 900, 150), {}}, "annotate", false);
        overlay->show();
        QSignalSpy result(overlay, &Overlay::accepted);
        QTest::mousePress(overlay, Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(overlay, QPoint(700, 80));
        QTest::mouseRelease(overlay, Qt::LeftButton, {}, QPoint(700, 80));
        QTest::keyClick(overlay, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(result.count(), 1); QCOMPARE(result[0][2].toString(), QString("quickSave"));
        QCOMPARE(qvariant_cast<QImage>(result[0][0]).size(), QSize(680, 60));
    }
    void ordinaryEditorHasNoApplyAction() {
        auto *editor = new Editor(whiteImage(), {}, "en-US");
        editor->show();
        for (auto *action : editor->findChildren<QAction *>())
            if (action->text() == QStringLiteral("应用并关闭")) QVERIFY(!action->isVisible());
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
        const QStringList actions{"edit", "copy", "save", "quickSave", "pin", "ocr", "scroll"};
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
        for (int glyph = 0; glyph <= int(ToolIcon::FlipVertical); ++glyph) {
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
        const QString tag = qEnvironmentVariable("STILL_TOOLBAR_SNAPSHOT_TAG", "current");
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
