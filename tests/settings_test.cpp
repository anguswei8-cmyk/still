#include "app/application.h"
#include "clipboard/clipboard_archive.h"
#include "clipboard/clipboard_controller.h"
#include "imaging/editor.h"
#include "storage/history.h"
#include "ui/main_window.h"
#include "capture/overlay.h"
#include "imaging/pin.h"
#include "settings/settings_apply.h"
#include "capture/scroll_session.h"
#include "storage/store.h"
#include "tracked_image.h"
#include "ui/i18n.h"
#include <QAction>
#include <QStyleHints>

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QCheckBox>
#include <QDialog>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QLabel>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTextLayout>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace still;
namespace still { void setPasteTargetForTest(quintptr target); quintptr lastPasteTargetForTest(); }
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *match = findItem(child, name)) return match;
    return nullptr;
}

class SettingsTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QByteArray previousDirectory_;
    Application *controller_ = nullptr;
    MainWindow *window_ = nullptr;
    SettingsApply *apply_ = nullptr;
    QFont initialBodyFont_;
    QQuickItem *item(const QString &name) const {
        return findItem(window_->view()->quickWindow()->contentItem(), name);
    }
    void click(QQuickItem *control, Qt::MouseButton button = Qt::LeftButton) {
        QVERIFY(control); QVERIFY(control->isVisible());
        QTest::mouseClick(window_->view(), button, {}, control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
    }
    void movePointerTo(QQuickItem *control) {
        // Send to the QML window so hover does not depend on the desktop cursor.
        QTest::mouseMove(window_->view()->quickWindow(), control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
    }
    void locateWindow() {
        window_ = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = qobject_cast<MainWindow *>(widget)) window_ = window;
    }
    void openPage(int page) {
        const bool preferences = page != 0 && page != 3;
        const bool currentPreferences = window_->page() != 0 && window_->page() != 3;
        if (preferences && !currentPreferences) click(item("PreferencesEntry"));
        if (!preferences && currentPreferences) click(item("BackToLibrary"));
        QTRY_VERIFY(item("Navigation" + QString::number(page)));
        click(item("Navigation" + QString::number(page)));
    }
    QString reviewPath(const QString &name) const {
        const QString path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.impeccable/review")
            + (QGuiApplication::platformName() == "offscreen" ? "/offscreen" : qEnvironmentVariable("QT_SCALE_FACTOR") == "1.5" ? "/hidpi" : "");
        QDir().mkpath(path); return path + "/qml-" + name + ".png";
    }
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        // The offscreen plugin does not discover Windows fonts, so use the same UI metrics as the app.
        if (QGuiApplication::platformName() == "offscreen") {
            for (const auto &name : {"msyh.ttc", "msyhbd.ttc"})
                QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QString("Fonts/") + name));
        }
#endif
        QVERIFY(directory_.isValid()); previousDirectory_ = qgetenv("PIXCLONE_DATA_DIR");
        qputenv("PIXCLONE_DATA_DIR", directory_.path().toUtf8());
        QVERIFY(writeJson(settingsPath(), {{"capture", QJsonObject{{"afterSelectAction", "unknown"}}}, {"ocr", QJsonObject{{"language", false}}}, {"future", QJsonObject{{"keep", 42}}}, {"hotkeys", QJsonObject{{"clipboardHistory", "Ctrl+Shift+Space"}}}, {"appearance", QJsonObject{{"language", "zh_CN"}, {"theme", "light"}}}}));
        controller_ = new Application(nullptr, false); QTest::qWait(100); locateWindow();
        QVERIFY(window_); QCOMPARE(window_->view()->status(), QQuickWidget::Ready);
        QVERIFY(window_->view()->rootObject()); QCOMPARE(window_->page(), 0);
        QVERIFY(item("ClipboardSearch")); QVERIFY(!window_->findChild<QWidget *>("MainSidebar"));
        QCOMPARE(window_->form().value("capture").toMap().value("afterSelectAction").toString(), QString("annotate"));
        QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("zh-Hans-CN"));
        apply_ = controller_->findChild<SettingsApply *>(); QVERIFY(apply_);
        if (QGuiApplication::platformName() == "windows")
            QCOMPARE(QFontInfo(item("PageTitle")->property("font").value<QFont>()).family(), QString("Microsoft YaHei UI"));
    }
    void sidebarMouseKeyboardAndEntryPoints() {
        const QStringList titles{QStringLiteral("剪贴板"), QStringLiteral("通用偏好"), QStringLiteral("快捷键"), QStringLiteral("截图历史"), QStringLiteral("识别与限制"), QStringLiteral("剪贴板设置")};
        for (int page = 0; page < titles.size(); ++page) {
            openPage(page); QCOMPARE(window_->page(), page);
            QCOMPARE(item("PageTitle")->property("text").toString(), titles[page]);
            auto *capture = item("MainCapture"); QVERIFY(capture);
            QCOMPARE(capture->isVisible(), page == 0 || page == 3);
            if (capture->isVisible()) {
                const QRectF captureBounds(capture->mapToScene(QPointF()), capture->size());
                QCOMPARE(captureBounds.bottomRight(), QPointF(window_->view()->width() - 24, window_->view()->height() - 24));
                if (page == 0) {
                    auto *clipboard = item("ClipboardPanel"); QVERIFY(clipboard);
                    QVERIFY(clipboard->mapToScene(QPointF(0, clipboard->height())).y() > captureBounds.top());
                }
            }
            if (page != 0 && page != 3) {
                QVERIFY(item("BackToLibrary")->isVisible());
                QTRY_VERIFY(item("BackToLibrary")->mapToScene(QPointF()).y() < item("MainNavigation")->mapToScene(QPointF()).y());
            }
        }
        controller_->openSettings(); QCOMPARE(window_->page(), 5);
        auto *navigation = item("MainNavigation"); navigation->forceActiveFocus(); window_->setPage(1);
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->page(), 2);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->page(), 1);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->page(), 1);
        click(item("BackToLibrary")); window_->setPage(0); navigation->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->filter(), QString("pinned"));
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->page(), 3);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->filter(), QString("pinned"));
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->filter(), QString("all"));
        controller_->openClipboard(); QCOMPARE(window_->page(), 0);
        QVERIFY(item("ClipboardSearch")->hasActiveFocus());
        for (int i = 0; i < 30 && !item("ClipboardList")->hasActiveFocus(); ++i) QTest::keyClick(window_->view(), Qt::Key_Tab);
        QVERIFY(item("ClipboardList")->hasActiveFocus());
        QVERIFY(!item("ClipboardSettings"));
        openPage(5); QCOMPARE(window_->page(), 5);
        QVERIFY(item("clipboard_enabled")->isVisible());
        QPointer<MainWindow> closed = window_;
        window_->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(closed.isNull());
        auto *hotkeys = controller_->findChild<Hotkeys *>(); QVERIFY(hotkeys);
        const int clipboardHotkey = hotkeyNames.indexOf("clipboardHistory");
#ifdef Q_OS_WIN
        QCOMPARE(hotkeys->sequence(clipboardHotkey), QKeySequence("Ctrl+Alt+Space"));
#endif
        hotkeys->dispatch(clipboardHotkey);
        locateWindow(); QVERIFY(window_ && window_->isVisible());
        QCOMPARE(window_->page(), 0);
        QVERIFY(item("ClipboardSearch")->hasActiveFocus());
    }
    void nativeWindowControls() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Native window state checks require Windows");
        for (const auto &name : {"WindowMinimize", "WindowMaximize", "WindowClose"}) {
            auto *button = item(name); QVERIFY(button);
            auto *content = button->property("contentItem").value<QQuickItem *>();
            QVERIFY(content && !content->childItems().isEmpty());
            auto *icon = content->childItems().first();
            QCOMPARE(icon->mapToItem(button, QPointF(icon->width() / 2, icon->height() / 2)),
                     QPointF(button->width() / 2, button->height() / 2));
        }
        const QSize normal = window_->size();
        click(item("WindowMaximize")); QTRY_VERIFY(window_->isMaximized());
        click(item("WindowMaximize")); QTRY_VERIFY(!window_->isMaximized()); QTRY_COMPARE(window_->size(), normal);
        click(item("WindowMinimize")); QTRY_VERIFY(window_->isMinimized());
        window_->showNormal(); window_->activateWindow(); window_->view()->setFocus(); QTRY_VERIFY(!window_->isMinimized());
    }
    void clipboardSearchFilterPreviewAndNeighborSelection() {
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        for (const auto &text : {QStringLiteral("示例：项目交接要点\n截图、标注与复制操作保持一致。"), QStringLiteral("second sample"), QStringLiteral("https://example.com/design")}) {
            QMimeData mime; mime.setText(text); archive->add(captureClipboard(mime));
        }
        QImage picture(640, 360, QImage::Format_ARGB32); picture.fill(QColor("#dce8fa"));
        QMimeData image; image.setImageData(picture); archive->add(captureClipboard(image));
        QTRY_COMPARE(window_->clips().size(), 4);
        QCOMPARE(item("ClipTitle3")->property("text").toString(), QStringLiteral("示例：项目交接要点"));
        QVERIFY(item("ClipSnippet3")->property("text").toString().contains(QStringLiteral("截图、标注")));
        auto *search = item("ClipboardSearch"); click(search);
        QTest::keyClicks(window_->view(), "second"); QCOMPARE(window_->clips().size(), 1);
        click(item("Filter_image")); QCOMPARE(window_->clips().size(), 0);
        QVERIFY(!item("ClipboardPaste")->isEnabled());
        window_->setSearch({}); QCOMPARE(window_->clips().size(), 1);
        QSignalSpy ready(archive, &ClipboardArchive::ready);
        QTest::qWait(160); QVERIFY(ready.isEmpty());
        for (bool shown : {true, false}) {
            click(item("ClipboardMore"));
            QTRY_VERIFY(item("ClipboardMenuPreview") && item("ClipboardMenuPreview")->isVisible());
            click(item("ClipboardMenuPreview"));
            QTRY_COMPARE(window_->previewVisible(), shown);
            if (shown) QTRY_VERIFY(!window_->previewImage().isEmpty());
        }
        QVERIFY(window_->previewImage().isEmpty());
        window_->setFilter("all"); window_->setSelectedIndex(1);
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->selectedIndex(), 2);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->selectedIndex(), 1);
        click(item("ClipRow1"), Qt::RightButton);
        auto *menu = window_->view()->rootObject()->findChild<QObject *>("ClipboardContextMenu");
        QVERIFY(menu); QTRY_VERIFY(menu->property("visible").toBool());
        QMetaObject::invokeMethod(menu, "close"); QTRY_VERIFY(!menu->property("visible").toBool());
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Space); QVERIFY(window_->previewVisible());
        QTest::keyClick(window_->view(), Qt::Key_Space); QVERIFY(!window_->previewVisible());
        const QString neighbor = window_->clips()[2].toMap().value("id").toString();
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Delete);
        QTRY_COMPARE(window_->clips().size(), 3); QCOMPARE(window_->selectedIndex(), 1);
        QCOMPARE(window_->selected().value("id").toString(), neighbor);
        click(item("Pin" + QString::number(window_->selectedIndex())));
        QTRY_VERIFY(window_->selected().value("pinned").toBool());
        click(item("NavigationPinned")); QCOMPARE(window_->clips().size(), 1);
        window_->setFilter("all");
    }
    void clipboardHoverActionsStayCenteredAndDeleteHoveredItem() {
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        const int count = window_->clips().size();
        QMimeData mime; mime.setText("Temporary hover sample"); archive->add(captureClipboard(mime));
        QTRY_COMPARE(window_->clips().size(), count + 1);
        const auto clips = window_->clips();
        int hoveredIndex = 0;
        while (hoveredIndex < clips.size() && clips[hoveredIndex].toMap().value("title").toString() != mime.text()) ++hoveredIndex;
        QVERIFY(hoveredIndex < clips.size());
        const int otherIndex = hoveredIndex == 0 ? 1 : 0;
        const QString hoveredId = clips[hoveredIndex].toMap().value("id").toString();
        const auto cleanup = qScopeGuard([&] { archive->remove(hoveredId); });
        const QString suffix = QString::number(hoveredIndex);
        movePointerTo(item("ClipboardSearch"));
        window_->setSelectedIndex(hoveredIndex);
        for (const auto &name : {"Copy", "Pin", "Delete"}) {
            auto *button = item(name + suffix); QVERIFY(button); QTRY_VERIFY(button->isVisible());
        }
        window_->setSelectedIndex(otherIndex);
        QVERIFY(hoveredId != window_->selected().value("id").toString());
        QTRY_VERIFY(!item("Copy" + suffix)->isVisible() && !item("Delete" + suffix)->isVisible());
        auto center = [](QQuickItem *control) { return QPointF(control->width() / 2, control->height() / 2); };
        auto *row = item("ClipRow" + suffix); QVERIFY(row);
        movePointerTo(row);
        for (const auto &name : {"Copy", "Pin", "Delete"}) {
            auto *button = item(name + suffix); QVERIFY(button); QTRY_VERIFY(button->isVisible());
            movePointerTo(button);
            QTRY_VERIFY(button->isVisible() && button->property("hovered").toBool());
            auto *content = button->property("contentItem").value<QQuickItem *>();
            QVERIFY(content && !content->childItems().isEmpty());
            auto *icon = content->childItems().first();
            QTRY_COMPARE(icon->mapToItem(button, center(icon)), center(button));
        }
        if (QGuiApplication::platformName() == "offscreen") {
            click(item("Copy" + suffix));
            QTRY_COMPARE(QGuiApplication::clipboard()->text(), mime.text());
            QGuiApplication::clipboard()->clear();
            window_->setSelectedIndex(otherIndex);
        }
        click(item("Delete" + suffix));
        QTRY_COMPARE(window_->clips().size(), count);
        for (const auto &entry : window_->clips()) QVERIFY(entry.toMap().value("id").toString() != hoveredId);
    }
    void qmlCopyShortcutAndReliablePaste() {
        if (QGuiApplication::platformName() != "offscreen") QSKIP("Clipboard replay uses the isolated offscreen platform; native paste has a separate guarded test");
        window_->setSearch("second"); QTRY_COMPARE(window_->clips().size(), 1);
        QGuiApplication::clipboard()->clear();
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second sample"));
        QVERIFY(window_->isVisible());
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(window_->view(), Qt::Key_Tab);
        QTest::keyClick(window_->view(), Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second sample"));
        click(item("ClipboardSearch"));
        QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window_->view(), Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second"));
        window_->hide(); setPasteTargetForTest(42); controller_->openClipboard();
        QVERIFY(window_->pasteDefault());
        QTest::keyClick(window_->view(), Qt::Key_Return);
        QTRY_VERIFY(!window_->isVisible()); QTRY_COMPARE(lastPasteTargetForTest(), quintptr(42));
        controller_->openClipboard(); window_->setSearch({});
        QEvent deactivate(QEvent::WindowDeactivate); QApplication::sendEvent(window_, &deactivate);
        QVERIFY(!window_->pasteDefault());
        QVERIFY(!item("ClipboardPaste")->isVisible());
        QGuiApplication::clipboard()->clear();
    }
    void preferenceClickSavesAndValidates() {
        window_->setPage(1); QTest::qWait(20);
        QVERIFY(!item("SettingsStatus")->isVisible());
        QCOMPARE(item("capture_afterSelectAction")->property("currentText").toString(), QStringLiteral("打开编辑器"));
        auto *magnifier = item("capture_showMagnifier"); QVERIFY(magnifier);
        QSignalSpy finished(apply_, &SettingsApply::finished);
        click(magnifier); QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QVERIFY(!readJson(settingsPath()).value("capture").toObject().value("showMagnifier").toBool());
        QCOMPARE(readJson(settingsPath()).value("future").toObject().value("keep").toInt(), 42);
        window_->setSetting("clipboard", "maxItems", 0);
        QCOMPARE(window_->form().value("clipboard").toMap().value("maxItems").toInt(), 200);
        window_->setSetting("clipboard", "maxItems", 42);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("clipboard").toObject().value("maxItems").toInt(), 42);
        QVERIFY(!item("SettingsStatus")->isVisible());
    }
    void settingsFlickStopsAtBothBounds() {
        window_->setPage(1);
        QTRY_VERIFY(item("SettingsPage"));
        auto *page = item("SettingsPage");
        auto *flickable = page->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        QTRY_VERIFY(flickable->property("contentHeight").toReal() > flickable->height());
        const qreal top = flickable->property("contentY").toReal();
        QVERIFY(QMetaObject::invokeMethod(flickable, "flick", Q_ARG(qreal, 0.0), Q_ARG(qreal, 1600.0)));
        QTest::qWait(100);
        QCOMPARE(flickable->property("contentY").toReal(), top);
        const qreal bottom = top + flickable->property("contentHeight").toReal() - flickable->height();
        QVERIFY(flickable->setProperty("contentY", bottom));
        QVERIFY(QMetaObject::invokeMethod(flickable, "flick", Q_ARG(qreal, 0.0), Q_ARG(qreal, -1600.0)));
        QTest::qWait(100);
        QCOMPARE(flickable->property("contentY").toReal(), bottom);
    }
    void completedSavePreservesUnfinishedInput() {
        const QString original = window_->form().value("ocr").toMap().value("language").toString();
        window_->setDraft("ocr", "language", "en-US");
        QSignalSpy finished(apply_, &SettingsApply::finished);
        window_->setSetting("clipboard", "enabled", false);
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("en-US"));
        QCOMPARE(apply_->settings().value("ocr").toObject().value("language").toString(), original);
        window_->commitDrafts(); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("ocr").toObject().value("language").toString(), QString("en-US"));
        window_->setSetting("clipboard", "enabled", true); QTRY_VERIFY(!apply_->busy());
    }
    void rejectedHotkeyKeepsPreviousValue() {
        window_->setPage(2); QTest::qWait(20);
        const QString original = apply_->settings().value("hotkeys").toObject().value("capture").toString();
        click(item("hotkey_capture")); QSignalSpy finished(apply_, &SettingsApply::finished);
        QTest::keyClick(window_->view(), Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QCOMPARE(apply_->settings().value("hotkeys").toObject().value("capture").toString(), original);
        QCOMPARE(item("hotkey_capture")->property("text").toString(), QKeySequence(original).toString(QKeySequence::NativeText));
        QVERIFY(window_->statusError());
        QVERIFY(item("SettingsStatus")->isVisible());
    }
    void actualCaptureCancellationRestoresWindows() {
        QWidget alreadyHidden;
        openPage(0);
        click(item("MainCapture"));
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000); QVERIFY(!window_->isVisible());
        QTest::keyClick(overlay(), Qt::Key_Escape); QTRY_VERIFY(window_->isVisible()); QVERIFY(!alreadyHidden.isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void actualCaptureOpensEditorWithCroppedPixels() {
        controller_->capture();
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000);
        QTest::mousePress(overlay(), Qt::LeftButton, {}, QPoint(50, 50)); QTest::mouseMove(overlay(), QPoint(150, 130)); QTest::mouseRelease(overlay(), Qt::LeftButton, {}, QPoint(150, 130)); QTest::keyClick(overlay(), Qt::Key_Return);
        auto editor = []() -> Editor * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Editor *>(widget)) return result; return nullptr; };
        QTRY_VERIFY(editor()); auto *canvas = editor()->findChild<Canvas *>(); QVERIFY(canvas && !canvas->rendered().isNull());
        QVERIFY(canvas->rendered().width() >= 100); QVERIFY(canvas->rendered().height() >= 80);
        editor()->close(); QVERIFY(window_->isVisible());
    }
    void scrollToolbarStartsForSelectedContent_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("edit") << false; QTest::newRow("cancel") << true;
    }
    void scrollToolbarStartsForSelectedContent() {
        QFETCH(bool, cancel);
        auto *session = controller_->findChild<ScrollSession *>(); QVERIFY(session);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const QRect region(screen.topLeft() + QPoint(40, 100), QSize(qMin(320, screen.width() - 80), qMin(220, screen.height() - 240)));
        const auto cleanup = qScopeGuard([&] {
            { QSignalBlocker blocked(session); session->stop(); }
            session->finished({}, region, {});
        });
        QString failure;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { failure = box->text(); box->accept(); }
        });
        dismiss.start(10); controller_->capture();
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000);
        auto *ordinaryToolbar = overlay()->findChild<QWidget *>("CaptureTools"); QVERIFY(ordinaryToolbar);
        ordinaryToolbar->adjustSize();
        const int ordinaryHeight = ordinaryToolbar->height();
        const QSize ordinaryButton = ordinaryToolbar->findChild<QToolButton *>("captureAction-edit")->size();
        QImage first(region.size(), QImage::Format_RGB32); first.fill(Qt::white);
        class ShowCounter : public QObject {
        public:
            int shows = 0;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Show) ++shows;
                return false;
            }
        } visibility;
        window_->installEventFilter(&visibility);
        overlay()->accepted(first, region, "scroll");
        QTest::qWait(50);
        QVERIFY2(session->active(), qPrintable(failure));
        QCOMPARE(visibility.shows, 0);
        QWidget *hud = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "ScrollHud") hud = widget;
        QVERIFY(hud); QVERIFY(hud->isVisible()); QVERIFY(!hud->frameGeometry().intersects(region));
        QCOMPARE(hud->findChild<QWidget *>("CaptureTools")->height(), ordinaryHeight);
        QCOMPARE(hud->findChild<QToolButton *>("ScrollFinish")->size(), ordinaryButton);
        if (QGuiApplication::platformName() == "windows") QVERIFY(!hud->isActiveWindow());
        QVERIFY(hud->findChild<QAbstractButton *>("ScrollFinish"));
        auto *selection = hud->findChild<QWidget *>("ScrollSelectionFrame");
        QVERIFY(selection); QVERIFY(selection->isVisible());
        QVERIFY(selection->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(hud->findChild<QLabel *>("ScrollImagePreview"));
        QCOMPARE(hud->findChild<QLabel *>("ScrollDimensions")->text(), QStringLiteral("%1 × %2").arg(first.width()).arg(first.height()));
        QVERIFY(!hud->findChild<QLabel *>("ScrollStatus"));
        auto *finish = hud->findChild<QAbstractButton *>("ScrollFinish");
        QCOMPARE(finish->text(), QStringLiteral("编辑"));
        QTest::mouseClick(cancel ? hud->findChild<QAbstractButton *>("scrollAction-cancel") : finish, Qt::LeftButton);
        QVERIFY(!session->active());
        QCOMPARE(visibility.shows, 1);
        Editor *editor = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Editor *>(widget); candidate && candidate->isVisible()) editor = candidate;
        if (cancel) { QVERIFY(!editor); QVERIFY(window_->isVisible()); return; }
        QVERIFY(editor); QVERIFY(editor->isVisible());
        QCOMPARE(editor->findChild<Canvas *>()->rendered().convertToFormat(QImage::Format_RGB32), first);
        QStringList actions;
        for (auto *action : editor->findChildren<QAction *>()) actions.append(action->text());
        QVERIFY(actions.contains(QStringLiteral("复制图片")));
        QVERIFY(actions.contains(QStringLiteral("保存")));
        QVERIFY(actions.contains(QStringLiteral("贴图")));
        editor->close();
    }
    void rejectedScrollReleasesFirstFrame() {
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept(); });
        dismiss.start(10);
        controller_->capture();
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000);
        overlay()->accepted(trackedImage(), QGuiApplication::primaryScreen()->geometry(), "scroll");
        QTest::qWait(50); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(liveTrackedImages.load(), 0); QVERIFY(window_->isVisible());
    }
    void doubleClickClosePreferencePersistsAndAppliesToNewPins_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme");
        QTest::newRow("zh-light") << "zh_CN" << "light";
        QTest::newRow("en-light") << "en" << "light";
        QTest::newRow("zh-dark") << "zh_CN" << "dark";
        QTest::newRow("en-dark") << "en" << "dark";
    }
    void doubleClickClosePreferencePersistsAndAppliesToNewPins() {
        QFETCH(QString, language); QFETCH(QString, theme);
        const auto original = apply_->settings();
        auto candidate = original;
        auto options = candidate.value("pin").toObject(); options["confirmDoubleClickClose"] = true; candidate["pin"] = options;
        apply_->apply(candidate);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(apply_->settings().value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(true));
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QApplication::setPalette(QGuiApplication::palette());
        QImage source(120, 80, QImage::Format_RGB32); source.fill(Qt::white);
        const auto create = [&]() -> Pin * {
            controller_->findChild<ClipboardController *>()->pinImage(source);
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget)) return pin;
            return nullptr;
        };
        QPointer<Pin> pin = create(); QVERIFY(pin);
        const auto cleanup = qScopeGuard([&] { delete pin.data(); });
        QSignalSpy disabled(pin, &Pin::doubleClickClosePromptDisabled);
        QSignalSpy stored(apply_, &SettingsApply::finished);
        bool prompted = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *box = pin ? pin->findChild<QDialog *>("PinClosePrompt") : nullptr; box && box->isVisible()) {
                prompted = true;
                auto *skip = box->findChild<QCheckBox *>("PinCloseSkip"); QVERIFY(skip); skip->setChecked(true);
                if (QGuiApplication::platformName() == "windows") {
                    QString path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.impeccable/review/actions");
                    if (qEnvironmentVariable("QT_SCALE_FACTOR") == "1.5") path += "/hidpi";
                    QDir().mkpath(path);
                    QVERIFY(box->grab().save(path + "/pin-close-" + language + "-" + theme + ".png"));
                }
                box->findChild<QPushButton *>("PinCloseConfirm")->click();
            }
        });
        dismiss.start(10);
        QTest::mouseDClick(pin, Qt::LeftButton, {}, QPoint(20, 20));
        QTRY_VERIFY(prompted); QTRY_VERIFY(pin.isNull());
        QCOMPARE(disabled.count(), 1);
        QTRY_VERIFY(!stored.isEmpty() && !apply_->busy());
        QVERIFY2(stored.last()[0].toBool(), qPrintable(stored.last()[3].toString()));
        QTRY_COMPARE(readJson(settingsPath()).value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(false));
        QTRY_COMPARE(apply_->settings().value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(false));
        prompted = false; pin = create(); QVERIFY(pin);
        QTest::mouseDClick(pin, Qt::LeftButton, {}, QPoint(20, 20));
        QTRY_VERIFY(pin.isNull()); QVERIFY(!prompted);
        apply_->apply(original); QTRY_VERIFY(!apply_->busy());
        InterfacePreferences::instance().apply(original.value("appearance").toObject());
        QApplication::setPalette(QGuiApplication::palette());
    }
    void pinClosePromptKeepsMainWindowBehindWorkspace_data() {
        QTest::addColumn<bool>("mainHidden"); QTest::addColumn<bool>("confirm");
        QTest::newRow("background-cancel") << false << false; QTest::newRow("hidden-cancel") << true << false;
        QTest::newRow("background-close") << false << true; QTest::newRow("hidden-close") << true << true;
    }
    void pinClosePromptKeepsMainWindowBehindWorkspace() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != "windows") QSKIP("Native Windows stacking test");
        QFETCH(bool, mainHidden); QFETCH(bool, confirm);
        QImage image(240, 120, QImage::Format_RGB32); image.fill(QColor("#e2e6ed"));
        QPointer<Pin> pin = new Pin(image, QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(60, 80), image.size()), {}, "en-US");
        const auto cleanup = qScopeGuard([&] { delete pin.data(); window_->show(); });
        pin->show();
        QWidget workspace; workspace.setWindowTitle("Still isolated workspace"); workspace.resize(640, 480); workspace.show(); workspace.raise(); workspace.activateWindow();
        const HWND mainHandle = reinterpret_cast<HWND>(window_->winId());
        const HWND workHandle = reinterpret_cast<HWND>(workspace.winId());
        if (mainHidden) window_->hide();
        else QVERIFY(SetWindowPos(mainHandle, workHandle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        const auto behind = [&] {
            for (HWND current = GetTopWindow(nullptr); current; current = GetWindow(current, GW_HWNDNEXT)) {
                if (current == workHandle) return true;
                if (current == mainHandle) return false;
            }
            return false;
        };
        QTest::qWait(80); if (!mainHidden) QVERIFY(behind());
        QTest::mouseDClick(pin, Qt::LeftButton, {}, QPoint(20, 20));
        auto *prompt = pin->findChild<QDialog *>("PinClosePrompt"); QVERIFY(prompt && prompt->isVisible());
        QCOMPARE(prompt->windowModality(), Qt::NonModal); QVERIFY(!QApplication::activeModalWidget());
        QTest::qWait(100);
        QVERIFY(!window_->isActiveWindow());
        if (mainHidden) QVERIFY(!window_->isVisible()); else QVERIFY(behind());
        if (confirm) {
            prompt->findChild<QPushButton *>("PinCloseConfirm")->click();
            QTRY_VERIFY(pin.isNull());
        } else {
            QTest::keyClick(prompt, Qt::Key_Escape);
            QTRY_VERIFY(!pin->findChild<QDialog *>("PinClosePrompt")); QVERIFY(pin->isVisible());
        }
        if (mainHidden) QVERIFY(!window_->isVisible()); else QVERIFY(behind());
#else
        QSKIP("Native Windows stacking test");
#endif
    }
    void historyPreferencesMigrateExistingFiles() {
        const QString source = historyDirectory(apply_->settings().value("history").toObject()); QDir().mkpath(source);
        QImage picture(160, 100, QImage::Format_ARGB32); picture.fill(QColor("#dce8fa"));
        QVERIFY(saveImage(picture, QDir(source).filePath("synthetic.png"), nullptr));
        const auto originalFiles = historyFiles(source); QVERIFY(!originalFiles.isEmpty());
        window_->setPage(3); QTRY_VERIFY(!window_->historyItems().isEmpty());
        click(item("Navigation3"));
        for (int i = 0; i < 60 && !item("HistoryList")->hasActiveFocus(); ++i) QTest::keyClick(window_->view(), Qt::Key_Tab);
        QVERIFY(item("HistoryList")->hasActiveFocus());
        QTest::keyClick(window_->view(), Qt::Key_Down);
        QVERIFY(item("HistoryList")->property("currentIndex").toInt() >= 0);
        const QString target = directory_.filePath("other-drive/history");
        click(item("HistoryStorage")); QTRY_VERIFY(item("history_dir"));
        QTest::qWait(80); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("history-storage")));
        click(item("history_dir")); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(window_->view(), target);
        QTest::keyClick(window_->view(), Qt::Key_Escape);
        QVERIFY(window_->isVisible());
        QTRY_COMPARE(readJson(settingsPath()).value("history").toObject().value("dir").toString(), target);
        window_->setSetting("history", "maxMiB", 64); window_->setSetting("history", "enabled", false);
        QTRY_VERIFY(!apply_->busy());
        const auto options = readJson(settingsPath()).value("history").toObject();
        QCOMPARE(options.value("dir").toString(), target); QCOMPARE(options.value("maxMiB").toInt(), 64); QVERIFY(!options.value("enabled").toBool());
        QVERIFY(historyFiles(source).isEmpty()); QCOMPARE(historyFiles(target).size(), originalFiles.size());
        QTRY_COMPARE(window_->historyItems().size(), originalFiles.size());
    }
    void invalidDirectoryRestoresPreviousValue() {
        const QString original = window_->form().value("history").toMap().value("dir").toString();
        const auto previous = readJson(settingsPath()); QFile occupied(directory_.filePath("occupied-file"));
        QVERIFY(occupied.open(QIODevice::WriteOnly)); occupied.write("keep this file"); occupied.close();
        QSignalSpy finished(apply_, &SettingsApply::finished);
        window_->setSetting("history", "dir", occupied.fileName() + "  ");
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy()); QVERIFY(!finished.last()[0].toBool());
        QCOMPARE(window_->form().value("history").toMap().value("dir").toString(), original);
        QCOMPARE(readJson(settingsPath()), previous); QVERIFY(window_->status().contains(QStringLiteral("未生效")));
        QVERIFY(!QApplication::activeModalWidget());
        click(item("HistoryStorage")); QTRY_VERIFY(item("HistoryStorageStatus"));
        QCOMPARE(item("HistoryStorageStatus")->property("text").toString(), window_->status());
        QTest::qWait(50); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("history-storage-error")));
        QPointer<MainWindow> openWindow = window_;
        QTest::keyClick(window_->view(), Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(openWindow); QVERIFY(openWindow->isVisible());
    }
    void successfulMigrationShowsCleanupWarning_data() {
        QTest::addColumn<int>("mode"); QTest::newRow("visible") << 0; QTest::newRow("hidden") << 1; QTest::newRow("pending-save") << 2;
    }
    void successfulMigrationShowsCleanupWarning() {
#ifdef Q_OS_WIN
        QFETCH(int, mode);
        if (mode != 0 && QSystemTrayIcon::isSystemTrayAvailable()) QSKIP("Notification fallback requires the offscreen platform");
        window_->setPage(3);
        const QString source = historyDirectory(apply_->settings().value("history").toObject());
        const QString path = QDir(source).filePath("locked-migration.png"); QDir().mkpath(source);
        QImage image(20, 20, QImage::Format_ARGB32); image.fill(Qt::white); QVERIFY(image.save(path));
        const auto closeHandle = [](void *handle) { CloseHandle(handle); };
        std::unique_ptr<void, decltype(closeHandle)> handle(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), closeHandle);
        QVERIFY(handle.get() != INVALID_HANDLE_VALUE);
        bool notified = false; QTimer dismissWarning;
        connect(&dismissWarning, &QTimer::timeout, this, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { notified = box->text().contains(QStringLiteral("旧副本未删除")); box->accept(); } });
        dismissWarning.start(10); if (mode == 1) window_->hide();
        QSignalSpy finished(apply_, &SettingsApply::finished);
        window_->setSetting("history", "dir", directory_.filePath(QStringLiteral("warning-history-%1").arg(mode)));
        if (mode == 2) window_->setSetting("history", "enabled", !window_->form().value("history").toMap().value("enabled").toBool());
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy()); dismissWarning.stop(); window_->show();
        QVERIFY(finished.last()[0].toBool()); const QString warning = finished.first()[3].toString(); QVERIFY(!warning.isEmpty()); QVERIFY(QFile::exists(path));
        if (mode == 0) { QVERIFY(window_->status().contains(warning)); QVERIFY(window_->statusError()); } else QVERIFY(notified);
#else
        QSKIP("Windows file sharing is required");
#endif
    }
    void deletingHistorySurvivesRefreshInsideConfirmation() {
        window_->setPage(3); window_->refreshHistory(); QTRY_VERIFY(!window_->historyItems().isEmpty());
        const QString path = window_->historyItems().first().toMap().value("path").toString();
        bool confirmed = false;
        QTimer::singleShot(0, this, [&] {
            window_->setPage(1); window_->setPage(3);
            if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { confirmed = true; question->button(QMessageBox::Yes)->click(); }
        });
        window_->historyAction(path, "remove"); QVERIFY(confirmed); QTRY_VERIFY(!QFile::exists(path));
    }
    void historyImageOpeningIsAsynchronousAndStopsOnNavigation() {
        auto *history = controller_->findChild<HistoryStore *>();
        QVERIFY(history);
        const auto path = QDir(history->directory()).filePath("open-async.png");
        QImage image(20, 20, QImage::Format_ARGB32); image.fill(Qt::white);
        QVERIFY(saveImage(image, path, nullptr));
        window_->setPage(3);
        window_->refreshHistory();
        QTRY_VERIFY(!window_->historyLoading() && !window_->historyItems().isEmpty());
        QSignalSpy opened(window_, &MainWindow::historyImageRequested);
        window_->historyAction(path, "edit");
        QVERIFY(opened.isEmpty());
        window_->setPage(0);
        QTest::qWait(100);
        QVERIFY(opened.isEmpty());
        window_->setPage(3);
        QTRY_VERIFY(!window_->historyLoading() && !window_->historyItems().isEmpty());
        window_->historyAction(path, "edit");
        QTRY_COMPARE(opened.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(opened.first()[0]).convertToFormat(image.format()), image);
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *editor = qobject_cast<Editor *>(widget)) editor->close();
        history->remove(path);
        QTRY_VERIFY(!QFile::exists(path));
    }
    void snapshotsAndCompactLayout() {
        window_->setPage(1); QTest::qWait(50);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("settings-warning")));
        window_->setSetting("appearance", "theme", "dark"); QTRY_VERIFY(!apply_->busy());
        window_->setSetting("appearance", "theme", "light"); QTRY_VERIFY(!apply_->busy());
        // Synthetic review content stays in this test's isolated clipboard archive.
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        for (const auto &text : {QStringLiteral("周末计划\n美术馆、河边散步，还有一家一直想去的书店。"),
                                QStringLiteral("Reading list\nA few pages for the train ride home."),
                                QStringLiteral("设计备忘\n让每个元素都有明确的职责，也给内容留一点呼吸空间。")}) {
            QMimeData mime; mime.setText(text); archive->add(captureClipboard(mime));
        }
        QMimeData logo; logo.setImageData(QImage(":/brand/still.png")); archive->add(captureClipboard(logo));
        QMimeData note; note.setText(QStringLiteral("留住眼前，随时找回\n图片、文字与文件，收好每一段值得留下的内容。")); archive->add(captureClipboard(note));
        const QString filePath = directory_.filePath("reading-list.md");
        QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Synthetic file for clipboard review."); file.close();
        QMimeData files; files.setUrls({QUrl::fromLocalFile(filePath)}); archive->add(captureClipboard(files));
        QTRY_COMPARE(window_->clips().size(), 9);
        for (int i = 0; i < window_->clips().size(); ++i) if (window_->clips()[i].toMap().value("preview").toString().startsWith(QStringLiteral("留住眼前"))) window_->setSelectedIndex(i);
        const QString selectedId = window_->selected().value("id").toString();
        window_->resize(1000, 700); window_->setSearch({}); window_->setFilter("all");
        const QStringList names{"clipboard", "general", "hotkeys", "history", "advanced", "clipboard-settings"};
        for (int page = 0; page < names.size(); ++page) {
            window_->setPage(page); QTest::qWait(80); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath(names[page])));
            if (page == 0) {
                auto *action = item("ClipboardMore");
                QVERIFY(action->mapToScene(QPointF()).y() + action->height() <= item("ClipboardList")->mapToScene(QPointF()).y());
                QVERIFY(item("ClipboardList")->mapToScene(QPointF()).y() < 170);
                initialBodyFont_ = item("ClipTitle0")->property("font").value<QFont>();
                QVERIFY(item("ClipRow0")->height() <= 80);
                QVERIFY(item("ClipRow0")->width() > item("ClipboardList")->width() - 20);
                QVERIFY(item("ClipRow5"));
                auto *list = item("ClipboardList"); QVERIFY(list);
                QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
                QTRY_VERIFY(item("ClipRow8"));
                auto *last = item("ClipRow8");
                QTRY_VERIFY(last->mapToScene(QPointF(0, last->height())).y() <= item("MainCapture")->mapToScene(QPointF()).y());
                QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
            }
            if (page == 3 && window_->historyItems().size() == 1) {
                auto *actions = item("HistoryActions"); QVERIFY(actions);
                const QRectF bounds(actions->mapToScene(QPointF()), actions->size());
                QVERIFY(window_->view()->rect().contains(bounds.toAlignedRect()));
            }
        }
        window_->setPage(0); click(item("Filter_files")); QTest::qWait(50);
        QCOMPARE(item("ClipTitle0")->property("text").toString(), QString("reading-list.md"));
        QVERIFY(item("ClipSnippet0")->property("text").toString().contains(QFileInfo(filePath).dir().dirName()));
        QCOMPARE(window_->selected().value("preview").toString(), filePath);
        window_->setPreviewVisible(true); QTRY_VERIFY(window_->previewText().contains("reading-list.md"));
        auto *selectedCard = item("ClipRow0"); auto *grid = item("ClipboardList"); QVERIFY(selectedCard); QVERIFY(grid);
        QTRY_VERIFY(QRectF(grid->mapToScene(QPointF()), grid->size()).contains(QRectF(selectedCard->mapToScene(QPointF()), selectedCard->size())));
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("files")));
        window_->setFilter("all");
        for (int i = 0; i < window_->clips().size(); ++i) if (window_->clips()[i].toMap().value("id").toString() == selectedId) window_->setSelectedIndex(i);
        QTest::qWait(200);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("preview")));
        window_->resize(800, 620); QTest::qWait(50);
        QCOMPARE(window_->size(), QSize(800, 620)); QVERIFY(!item("ClipboardPreview")->isVisible());
        QVERIFY(item("ClipboardPanel")->mapToScene(QPointF()).x() >= 200);
        selectedCard = item("ClipRow" + QString::number(window_->selectedIndex()));
        QTRY_VERIFY(QRectF(grid->mapToScene(QPointF()), grid->size()).contains(QRectF(selectedCard->mapToScene(QPointF()), selectedCard->size())));
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-clipboard")));
        window_->setPage(1); QTest::qWait(30); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-settings")));
        for (const auto &name : {QString("appearance_language"), QString("appearance_theme"), QString("capture_showMagnifier"), QString("capture_afterSelectAction")}) {
            auto *control = item(name); QVERIFY(control);
            const QRectF bounds(control->mapToScene(QPointF()), control->size()); QVERIFY(window_->view()->rect().contains(bounds.toAlignedRect()));
        }
        window_->resize(1000, 700);
    }
    void nativeGlassBackdrop() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Compositor material requires a real Windows desktop");
        const QString originalTheme = window_->form().value("appearance").toMap().value("theme").toString();
        const QRect available = window_->screen()->availableGeometry();
        window_->resize(qMin(1000, available.width() - 32), qMin(700, available.height() - 32));
        window_->move(available.center() - QPoint(window_->width() / 2, window_->height() / 2));
        QLabel backdrop(nullptr, Qt::Tool | Qt::FramelessWindowHint);
        backdrop.setGeometry(window_->geometry().adjusted(-8, -8, 8, 8));
        QPixmap pattern(backdrop.size());
        {
            QPainter painter(&pattern);
            const QStringList colors{"#abc8e4", "#e0b9bf", "#bcbce2", "#a9d0ce"};
            for (int i = 0; i < 4; ++i) painter.fillRect(i * pattern.width() / 4, 0, pattern.width() / 4 + 1, pattern.height(), QColor(colors[i]));
            painter.setPen(QPen(Qt::white, 3));
            for (int y = 20; y < pattern.height(); y += 32) painter.drawLine(0, y, pattern.width(), y);
        }
        backdrop.setPixmap(pattern); backdrop.show();
        window_->show(); window_->raise(); window_->activateWindow();
        for (const auto &theme : {QString("light"), QString("dark")}) {
            window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
            QTest::qWait(250);
            const QImage frame = window_->view()->grabFramebuffer(); QVERIFY(!frame.isNull());
            if (window_->glassAvailable()) QVERIFY(frame.pixelColor(16 * frame.devicePixelRatio(), 100 * frame.devicePixelRatio()).alpha() < 255);
            const auto shot = window_->screen()->grabWindow(0, window_->x(), window_->y(), window_->width(), window_->height());
            QVERIFY(!shot.isNull()); QVERIFY(shot.save(reviewPath("glass-native-" + theme)));
        }
        qInfo() << "Native backdrop enabled:" << window_->glassAvailable();
        window_->setSetting("appearance", "theme", originalTheme); QTRY_VERIFY(!apply_->busy());
    }
    void chineseUiGlyphsUseTheUiFont() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires installed Windows UI fonts");
        for (const auto weight : {QFont::Normal, QFont::Medium, QFont::DemiBold}) {
            QFont font = item("LibraryCount")->property("font").value<QFont>();
            font.setPixelSize(15); font.setWeight(weight);
            QTextLayout layout(QStringLiteral("留住眼前，随时找回"), font);
            layout.beginLayout(); layout.createLine(); layout.endLayout();
            const auto runs = layout.glyphRuns(); QVERIFY(!runs.isEmpty());
            for (const auto &run : runs) {
                qInfo() << "Chinese UI glyph family:" << font.family() << weight << run.rawFont().familyName();
                QCOMPARE(run.rawFont().familyName(), QString("Microsoft YaHei UI"));
            }
        }
    }
    void compactEnglishPasteActions() {
        const QString language = window_->form().value("appearance").toMap().value("language").toString();
        const QSize size = window_->size();
        window_->setSetting("appearance", "language", "en"); QTRY_VERIFY(!apply_->busy());
        window_->resize(800, 620); window_->hide(); setPasteTargetForTest(42); controller_->openClipboard();
        QTRY_VERIFY(window_->pasteDefault()); QTest::qWait(80);
        auto bounds = [](QQuickItem *control) { return QRectF(control->mapToScene(QPointF()), control->size()); };
        QVERIFY(bounds(item("Filter_other")).right() <= bounds(item("ClipboardMore")).left());
        QVERIFY(bounds(item("ClipboardMore")).right() <= bounds(item("ClipboardPaste")).left());
        QVERIFY2(QRectF(window_->view()->rect()).contains(bounds(item("ClipboardPaste"))), qPrintable(QDebug::toString(bounds(item("ClipboardPaste")))));
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-en-paste")));
        window_->setSetting("appearance", "language", language); QTRY_VERIFY(!apply_->busy());
        window_->resize(size);
    }
    void bilingualThemesAndControls() {
        window_->setPage(1); QTest::qWait(30);
        auto choose = [&](const QString &name, int steps) {
            QTest::qWait(40);
            if (name == "appearance_language") {
                click(item(steps > 0 ? "Language_en" : "Language_zh_CN"));
                return;
            }
            if (name == "appearance_theme") {
                const QStringList values{"system", "light", "dark"};
                const int current = values.indexOf(window_->form().value("appearance").toMap().value("theme").toString());
                click(item("Theme_" + values[qBound(0, current + steps, 2)]));
                return;
            }
            click(item(name));
            for (int i = 0; i < qAbs(steps); ++i) QTest::keyClick(window_->view(), steps > 0 ? Qt::Key_Down : Qt::Key_Up);
            QTest::keyClick(window_->view(), Qt::Key_Return);
        };
        choose("capture_afterSelectAction", 1);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("capture").toObject().value("afterSelectAction").toString(), QString("copy"));
        choose("capture_afterSelectAction", -1); QTRY_VERIFY(!apply_->busy());
        const auto clips = window_->clips();
        choose("appearance_language", 1);
        QTRY_COMPARE(item("PageTitle")->property("text").toString(), QString("General"));
        QCOMPARE(window_->windowTitle(), QString("Still"));
        QCOMPARE(item("ClipTitle0")->property("font").value<QFont>(), initialBodyFont_);
        QCOMPARE(item("capture_afterSelectAction")->property("currentText").toString(), QString("Open editor"));
        QCOMPARE(readJson(settingsPath()).value("appearance").toObject().value("language").toString(), QString("en"));
        QImage picture(400, 240, QImage::Format_RGB32); picture.fill(Qt::white);
        const QString path = directory_.filePath("language-check.png"); QVERIFY(picture.save(path));
        controller_->openImage(path); Editor *editor = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Editor *>(widget)) editor = candidate;
        QVERIFY(editor); QCOMPARE(editor->windowTitle(), QString("Still · Edit capture"));
        window_->activateWindow(); window_->view()->setFocus();
        choose("appearance_theme", 1);
        QTRY_VERIFY(!apply_->busy());
        QTRY_VERIFY(window_->dark()); QVERIFY(QGuiApplication::palette().color(QPalette::Window).lightness() < 60);
        QCOMPARE(editor->palette().color(QPalette::Window), QGuiApplication::palette().color(QPalette::Window));
        QCOMPARE(readJson(settingsPath()).value("appearance").toObject().value("theme").toString(), QString("dark"));
        for (int page = 0; page <= 5; ++page) {
            window_->setPage(page); QTest::qWait(80);
            if (page == 0) {
                auto *grid = item("ClipboardList"); auto *selectedCard = item("ClipRow" + QString::number(window_->selectedIndex()));
                QVERIFY(grid); QVERIFY(selectedCard);
                QTRY_VERIFY(QRectF(grid->mapToScene(QPointF()), grid->size()).contains(QRectF(selectedCard->mapToScene(QPointF()), selectedCard->size())));
            }
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-en-" + QString::number(page))));
        }
        window_->resize(800, 620);
        for (int page : {0, 1, 5}) {
            window_->setPage(page); QTest::qWait(50);
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-en-" + QString::number(page))));
        }
        window_->resize(1000, 700); window_->setPage(1);
        choose("appearance_theme", -1); QTRY_VERIFY(!window_->dark());
        QTest::qWait(50); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("light-en-general")));
        choose("appearance_language", -1);
        QTRY_COMPARE(item("PageTitle")->property("text").toString(), QStringLiteral("通用偏好"));
        QCOMPARE(editor->windowTitle(), QStringLiteral("拾影 · 编辑截图")); editor->close();
        choose("appearance_theme", -1); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(window_->dark(), QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        window_->setPage(5);
        auto *number = item("clipboard_maxItems"); QVERIFY(number);
        const int before = number->property("value").toInt(); number->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Up); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("clipboard").toObject().value("maxItems").toInt(), before + 1);
        QTest::keyClick(window_->view(), Qt::Key_Down); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("clipboard").toObject().value("maxItems").toInt(), before);
        QCOMPARE(window_->clips().size(), clips.size());
        for (int i = 0; i < clips.size(); ++i) if (clips[i].toMap().value("kind") == "text") QCOMPARE(window_->clips()[i].toMap().value("title"), clips[i].toMap().value("title"));
        window_->setPage(1);
    }
    void darkInteractionStates() {
        const QString previous = window_->form().value("appearance").toMap().value("theme").toString();
        window_->setSetting("appearance", "theme", "dark"); QTRY_VERIFY(!apply_->busy()); QTRY_VERIFY(window_->dark());
        window_->setPage(0); window_->togglePause(); QTRY_VERIFY(window_->paused()); QTest::qWait(50);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-paused")));
        if (QGuiApplication::platformName() == "windows") {
            auto *row = item("ClipRow" + QString::number(window_->selectedIndex())); QVERIFY(row);
            movePointerTo(row);
            auto *remove = item("Delete" + QString::number(window_->selectedIndex())); QVERIFY(remove);
            QTRY_VERIFY(remove->isVisible());
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-row-actions")));
        }
        auto *capture = item("MainCapture"); QVERIFY(capture);
        const QPoint point = capture->mapToScene(QPointF(capture->width() / 2, capture->height() / 2)).toPoint();
        QTest::mousePress(window_->view(), Qt::LeftButton, {}, point); QVERIFY(capture->property("down").toBool()); QTest::qWait(50);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-pressed")));
        // Release outside the capture button so this state check does not start a capture.
        QTest::mouseMove(window_->view(), QPoint(300, 30));
        QTest::mouseRelease(window_->view(), Qt::LeftButton, {}, QPoint(300, 30));
        QVERIFY(!capture->property("down").toBool());
        QVERIFY(window_->isVisible());
        window_->togglePause(); QVERIFY(!window_->paused());
        window_->setPage(1); QTest::qWait(30);
        auto *toggle = item("capture_showMagnifier"); QVERIFY(toggle); toggle->forceActiveFocus();
        QVERIFY(toggle->hasActiveFocus());
        auto *indicator = toggle->property("indicator").value<QQuickItem *>(); QVERIFY(indicator);
        QCOMPARE(indicator->property("border").value<QObject *>()->property("color").value<QColor>(), QColor("#a8a8a8"));
        QTest::qWait(50); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-switch-focus")));
        window_->setSetting("appearance", "theme", previous); QTRY_VERIFY(!apply_->busy()); window_->setPage(1);
    }
    void contextMenuFollowsAppearanceAndInvokesAction() {
        window_->activateWindow(); window_->view()->setFocus();
        const QString previous = window_->form().value("appearance").toMap().value("theme").toString();
        for (const QString &theme : {QString("light"), QString("dark")}) {
            window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
            window_->setPage(0); click(item("ClipboardMore"));
            auto *menu = window_->view()->rootObject()->findChild<QObject *>("ClipboardContextMenu"); QVERIFY(menu);
            QTRY_VERIFY(menu->property("opened").toBool());
            QPointer<MainWindow> openWindow = window_;
            QTest::keyClick(window_->view(), Qt::Key_Escape);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(openWindow); QVERIFY(openWindow->isVisible()); QTRY_VERIFY(!menu->property("visible").toBool());
            click(item("ClipboardMore")); QTRY_VERIFY(menu->property("visible").toBool());
            QTRY_VERIFY(item("ClipboardMenuCopy"));
            QCOMPARE(item("ClipboardMenuSurface")->property("color").value<QColor>(), QColor(theme == "dark" ? "#1c1c1c" : "#ffffff"));
            auto *label = item("ClipboardMenuCopy")->property("contentItem").value<QQuickItem *>(); QVERIFY(label);
            QCOMPARE(label->property("color").value<QColor>(), QColor(theme == "dark" ? "#ededed" : "#242731"));
            QTest::keyClick(window_->view(), Qt::Key_Down); QTest::qWait(50);
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("menu-" + theme)));
            QTest::keyClick(window_->view(), Qt::Key_Escape); QTRY_VERIFY(!menu->property("visible").toBool());
            QVERIFY(window_->isVisible());
            const bool pinned = window_->selected().value("pinned").toBool();
            click(item("ClipboardMore")); QTRY_VERIFY(menu->property("visible").toBool());
            click(item("ClipboardMenuPin")); QTRY_COMPARE(window_->selected().value("pinned").toBool(), !pinned);
            QTRY_VERIFY(!menu->property("visible").toBool()); window_->togglePin();
        }
        window_->setSetting("appearance", "theme", previous); QTRY_VERIFY(!apply_->busy()); window_->setPage(1);
    }
    void settingsCommitSurvivesClosingWindow() {
        QSignalSpy finished(apply_, &SettingsApply::finished);
        QPointer<MainWindow> previous = window_;
        window_->setDraft("ocr", "language", "zh-Hans-CN"); window_->setSetting("clipboard", "pasteOnSelect", false);
        item("capture_showMagnifier")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(previous.isNull());
        window_ = nullptr; QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("ocr").toObject().value("language").toString(), QString("zh-Hans-CN"));
        QVERIFY(!readJson(settingsPath()).value("clipboard").toObject().value("pasteOnSelect").toBool());
        controller_->openSettings(); locateWindow(); QVERIFY(window_); QCOMPARE(window_->view()->status(), QQuickWidget::Ready);
        QCOMPARE(window_->page(), 1);
    }
    void cleanupTestCase() {
        delete controller_;
        if (previousDirectory_.isNull()) qunsetenv("PIXCLONE_DATA_DIR"); else qputenv("PIXCLONE_DATA_DIR", previousDirectory_);
    }
};
QTEST_MAIN(SettingsTest)
#include "settings_test.moc"
