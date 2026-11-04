#include "ui/i18n.h"
#include "application.h"
#include "imaging/editor.h"
#include "imaging/ocr.h"
#include "capture/overlay.h"
#include "imaging/pin.h"
#include "storage/store.h"
#include "storage/history.h"
#include "settings/settings_apply.h"
#include "capture/scroll_session.h"
#include "capture/scroll_capture_ui.h"
#include "clipboard/clipboard_controller.h"
#include "ui/main_window.h"
#include "ui/toolicons.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaProperty>
#include <QAction>
#include <QToolBar>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace pixclone {

static QJsonObject defaultHotkeys() {
#ifdef Q_OS_WIN
    constexpr auto clipboardHistory = "Ctrl+Alt+Space";
#else
    constexpr auto clipboardHistory = "Ctrl+Shift+Space";
#endif
    return {{"capture", "Ctrl+1"}, {"quickCapture", "Ctrl+2"}, {"fullScreen", "Ctrl+3"}, {"pasteAsPin", "Ctrl+Shift+V"},
            {"togglePins", "Ctrl+4"}, {"scrollCapture", "Ctrl+5"}, {"closePins", "Ctrl+Shift+Q"}, {"clipboardHistory", clipboardHistory}};
}

QString Application::settingString(const QString &group, const QString &key, const QString &fallback) const {
    return settingsApply_->settings().value(group).toObject().value(key).toString(fallback);
}
int Application::settingInt(const QString &group, const QString &key, int fallback) const {
    return settingsApply_->settings().value(group).toObject().value(key).toInt(fallback);
}
bool Application::settingBool(const QString &group, const QString &key, bool fallback) const {
    return settingsApply_->settings().value(group).toObject().value(key).toBool(fallback);
}

Application::Application(QObject *parent, bool monitorClipboard) : QObject(parent), hotkeys_(this), tray_(this) {
    QJsonObject initial = readJson(settingsPath());
    if (initial.isEmpty()) {
        // Read legacy preferences once, into a separate file; never rewrite the Electron settings.
        initial = readJson(QDir(dataDirectory()).filePath("settings.json"));
    }
    auto bindings = defaultHotkeys();
    const auto savedKeys = initial.value("hotkeys").toObject();
    for (auto it = savedKeys.begin(); it != savedKeys.end(); ++it) {
#ifdef Q_OS_WIN
        // Once saved by this version, an explicit choice of the old combination must survive reopening.
        if (it.key() == "clipboardHistory" && it.value() == "Ctrl+Shift+Space" && !initial.value("clipboardShortcutMigrated").toBool()) continue;
#endif
        bindings[it.key()] = it.value();
    }
#ifdef Q_OS_WIN
    initial["clipboardShortcutMigrated"] = true;
#endif
    initial["hotkeys"] = bindings;
    auto &preferences = InterfacePreferences::instance();
    preferences.apply(initial.value("appearance").toObject());
    QApplication::setPalette(QGuiApplication::palette());
    initial = normalizeSettings(initial);
    connect(&preferences, &InterfacePreferences::changed, this, [this] {
        const auto &preferences = InterfacePreferences::instance();
        QApplication::setPalette(QGuiApplication::palette());
        tray_.setToolTip(t("拾影 · 截图与剪贴板"));
        QList<QObject *> objects;
        for (auto *widget : QApplication::topLevelWidgets()) { objects.append(widget); objects.append(widget->findChildren<QObject *>()); }
        if (tray_.contextMenu()) { objects.append(tray_.contextMenu()); objects.append(tray_.contextMenu()->findChildren<QObject *>()); }
        // Only translate chrome, never editable text, clipboard contents or annotation text.
        for (auto *object : objects) {
            if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->objectName() == "CaptureTools") widget->setStyleSheet(toolbarStyleSheet());
            const bool caption = qobject_cast<QAction *>(object) || qobject_cast<QAbstractButton *>(object) || qobject_cast<QLabel *>(object);
            for (const char *key : {"text", "title", "windowTitle", "toolTip", "accessibleName", "accessibleDescription"}) {
                if (qstrcmp(key, "text") == 0 && !caption) continue;
                const auto value = object->property(key);
                if (value.metaType().id() == QMetaType::QString) object->setProperty(key, preferences.translatedCaption(value.toString()));
            }
        }
    });
    clipboardArchive_ = new ClipboardArchive(initial.value("clipboard").toObject(), this);
    history_ = new HistoryStore(initial.value("history").toObject(), this);
    settingsApply_ = new SettingsApply(initial, settingsPath(), hotkeys_, *history_, this, clipboardArchive_);
    clipboard_ = new ClipboardController(*clipboardArchive_, initial.value("clipboard").toObject(), monitorClipboard, this);
    connect(clipboard_, &ClipboardController::message, this, [this](const QString &message) { notify(message); });
    connect(clipboard_, &ClipboardController::editImage, this, [this](const QImage &image) { createEditor(image)->show(); });
    connect(clipboard_, &ClipboardController::pinImage, this, [this](const QImage &image) { createPin(image); });
    connect(clipboard_, &ClipboardController::saveImage, this, [this](const QImage &image) { if (saveWithDialog(nullptr, image, settingString("save", "dir"))) remember(image); });
    connect(clipboard_, &ClipboardController::settingsRequested, this, [this] {
        openSettings();
        if (settingsWindow_) settingsWindow_->setPage(5);
    });
    connect(clipboard_, &ClipboardController::dismissRequested, this, [this] { if (settingsWindow_) settingsWindow_->hide(); });
    connect(settingsApply_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &settings, const QStringList &, const QString &) {
        if (success) { clipboard_->configure(settings.value("clipboard").toObject()); InterfacePreferences::instance().apply(settings.value("appearance").toObject()); }
    });
    connect(settingsApply_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &, const QStringList &, const QString &message) {
        // A pending save suppresses the page's result display, so warnings still need a notification.
        if ((!success || !message.isEmpty()) && (!settingsWindow_ || !settingsWindow_->isVisible() || settingsApply_->busy())) notify(message, true);
    });
    connect(settingsApply_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &, const QStringList &, const QString &) { if (success) applyPinSettings(); });
    connect(history_, &HistoryStore::errorOccurred, this, [this](const QString &message) { notify(message, true); });
    const QIcon appIcon(":/brand/still.png");
    QApplication::setWindowIcon(appIcon);
    tray_.setIcon(appIcon);
    tray_.setToolTip(t("拾影 · 截图与剪贴板"));
    auto *menu = new QMenu;
    menu->addAction(t("截图并编辑"), this, [this] { capture(); });
    menu->addAction(t("快速截图复制"), this, [this] { capture("copy"); });
    menu->addAction(t("整屏截图并贴图"), this, [this] { capture("fullScreen"); });
    menu->addAction(t("滚动长截图"), this, [this] { capture("scroll"); });
    menu->addAction(t("打开图片编辑…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(nullptr, t("打开图片"), {}, t("图像 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (!path.isEmpty()) openImage(path);
    });
    menu->addSeparator();
    menu->addAction(t("剪贴板图片贴图"), this, &Application::pastePin);
    menu->addAction(t("剪贴板历史…"), this, &Application::openClipboard);
    menu->addAction(t("显示/隐藏全部贴图"), this, &Application::togglePins);
    menu->addAction(t("关闭全部贴图"), this, &Application::closePins);
    menu->addSeparator();
    menu->addAction(t("设置…"), this, &Application::openSettings);
    menu->addAction(t("退出"), qApp, &QApplication::quit);
    tray_.setContextMenu(menu);
    connect(qApp, &QApplication::aboutToQuit, menu, &QObject::deleteLater);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) capture();
    });
    tray_.show();
    connect(&hotkeys_, &Hotkeys::activated, this, [this](int id) {
        switch (id) {
        case 0: capture(); break;
        case 1: capture("copy"); break;
        case 2: capture("fullScreen"); break;
        case 3: pastePin(); break;
        case 4: togglePins(); break;
        case 5: capture("scroll"); break;
        case 6: closePins(); break;
        case 7: openClipboard(); break;
        }
    });
    scroll_ = new ScrollSession([](const QRect &region) -> QImage {
        QScreen *screen = QGuiApplication::screenAt(region.center());
        if (!screen) return {};
        const QImage frame = screen->grabWindow(0).toImage();
        return cropFrame(frame, screen->size(), QRectF(region.translated(-screen->geometry().topLeft())));
    }, this);
    connect(scroll_, &ScrollSession::progress, this, [this](const QSize &, int) {
        if (scrollHud_) scrollHud_->setImage(scroll_->image());
    });
    connect(scroll_, &ScrollSession::alignmentPending, this, [this] {
        if (scrollHud_) scrollHud_->showAlignmentPending();
    });
    connect(scroll_, &ScrollSession::finished, this, &Application::finishScroll);
    // Always expose a normal window at startup, so permissions or hotkey conflicts cannot lock users out.
    QTimer::singleShot(0, this, &Application::openClipboard);
}

Application::~Application() {
    disconnect(scrollHudClosed_);
    if (scroll_) { scroll_->disconnect(this); scroll_->stop(); }
    delete scrollHud_.data();
    delete settingsWindow_.data();
    delete clipboard_;
    delete scroll_;
    // These children reference hotkeys_, whose C++ lifetime ends before QObject deletes children.
    delete settingsApply_;
    delete history_;
    delete clipboardArchive_;
}

void Application::openSettings() {
    openMainWindow();
    if (settingsWindow_ && (settingsWindow_->page() == 0 || settingsWindow_->page() == 3)) settingsWindow_->setPage(1);
}
void Application::openClipboard() {
    openMainWindow();
    if (!settingsWindow_) return;
    settingsWindow_->focusSearch();
}

void Application::openMainWindow() {
    if (!settingsWindow_ && settingsApply_->busy()) { notify(QStringLiteral("历史目录正在保存，完成后可重新打开设置")); return; }
    clipboard_->prepare();
    if (!settingsWindow_) {
        auto *window = new MainWindow(*settingsApply_, *history_, *clipboardArchive_, *clipboard_);
        settingsWindow_ = window;
        connect(window, &MainWindow::captureRequested, this, [this] { capture(); });
        connect(window, &MainWindow::historyImageRequested, this, [this](const QImage &image, bool pin) {
            if (pin) createPin(image); else createEditor(image)->show();
        });
    }
    settingsWindow_->show(); settingsWindow_->raise(); settingsWindow_->activateWindow();
}

void Application::notify(const QString &message, bool error) {
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_.showMessage(t("拾影"), message, error ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 3500);
    else if (error) QMessageBox::warning(settingsWindow_, t("拾影"), message);
}

void Application::capture(const QString &mode) {
    if (capturing_ || scroll_->active()) return;
    clipboard_->cancel();
    if (!requestCapturePermission()) {
        QMessageBox::information(settingsWindow_, t("需要屏幕录制权限"), t("请在系统设置 → 隐私与安全性 → 屏幕录制中允许拾影，然后重新启动应用。"));
        return;
    }
    capturing_ = true;
    hiddenForCapture_.clear();
    for (QWidget *window : QApplication::topLevelWidgets()) {
        if (window->isVisible() && !qobject_cast<QMenu *>(window)) { hiddenForCapture_.append(window); window->hide(); }
    }
    QTimer::singleShot(180, this, [this, mode] {
        QVector<CaptureFrame> frames;
        for (QScreen *screen : QGuiApplication::screens()) {
            QImage image = screen->grabWindow(0).toImage();
            if (image.isNull()) continue;
            image.setDevicePixelRatio(1);
            const int maxEdge = settingInt("capture", "maxEdge", 0);
            if (maxEdge > 0 && qMax(image.width(), image.height()) > maxEdge)
                image = image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            frames.append({image, screen->geometry(), settingBool("capture", "snapToWindows", true) ? windowRectangles(screen) : QVector<QRect>()});
        }
        if (frames.isEmpty()) { finishCapture(); notify(t("截图失败，请检查屏幕录制权限"), true); return; }
        if (mode == "fullScreen") {
            finishCapture();
            for (const auto &frame : frames) { createPin(frame.image, frame.geometry); remember(frame.image); }
            return;
        }
        const QString action = mode == "annotate" ? settingString("capture", "afterSelectAction", "annotate") : mode;
        for (auto &frame : frames) {
            auto *overlay = new Overlay(frame, action, settingBool("capture", "showMagnifier", true));
            overlays_.append(overlay);
            connect(overlay, &Overlay::accepted, this, [this](const QImage &image, const QRect &region, const QString &action) {
                // Copy values before closing the emitting overlay and freeing its frozen frame.
                const QImage result = image;
                const QRect bounds = region;
                const QString target = action;
                closeOverlays();
                QTimer::singleShot(0, this, [this, result, bounds, target] {
                    if (target == "scroll") startScroll(bounds, result);
                    else { finishCapture(); route(result, bounds, target); }
                });
            });
            connect(overlay, &Overlay::cancelled, this, &Application::finishCapture);
            overlay->show();
        }
        for (const auto &overlay : overlays_) {
            if (overlay && overlay->geometry().contains(QCursor::pos())) { overlay->raise(); overlay->activateWindow(); overlay->setFocus(); break; }
        }
    });
}

void Application::closeOverlays() {
    const auto windows = overlays_;
    overlays_.clear();
    for (const auto &window : windows) if (window) { window->blockSignals(true); window->close(); }
}

void Application::finishCapture() {
    if (!capturing_) return;
    closeOverlays();
    const auto hidden = std::move(hiddenForCapture_);
    for (const auto &window : hidden) if (window) window->show();
    capturing_ = false;
}

void Application::remember(const QImage &image) {
    history_->remember(image);
}

void Application::route(const QImage &image, const QRect &region, const QString &action) {
    if (action == "copy") { QApplication::clipboard()->setImage(image); remember(image); notify(t("已复制截图")); }
    else if (action == "save") { if (saveWithDialog(settingsWindow_, image, settingString("save", "dir"))) { remember(image); if (settingBool("save", "copyAfterSave", false)) QApplication::clipboard()->setImage(image); } }
    else if (action == "pin") { createPin(image, region); remember(image); }
    else if (action == "ocr") {
        delete ocrJob_.data();
        auto *job = new OcrJob(image, settingString("ocr", "language", "zh-Hans-CN"), this);
        ocrJob_ = job;
        connect(job, &OcrJob::finished, this, [this](const OcrResult &result) {
            const QString text = selectedText(result.words, 0, int(result.words.size()) - 1);
            if (!result.error.isEmpty() || text.isEmpty()) { notify(result.error.isEmpty() ? t("未识别到文字") : result.error, true); return; }
            QApplication::clipboard()->setText(text);
            notify(t("已复制识别文字"));
        });
    } else createEditor(image)->show();
}

Editor *Application::createEditor(const QImage &image) {
    auto *editor = new Editor(image, settingString("save", "dir"), settingString("ocr", "language", "zh-Hans-CN"));
    connect(editor, &Editor::imageCopied, this, &Application::remember);
    connect(editor, &Editor::imageSaved, this, [this](const QImage &result) {
        remember(result);
        if (settingBool("save", "copyAfterSave", false)) QApplication::clipboard()->setImage(result);
    });
    connect(editor, &Editor::pinRequested, this, [this](const QImage &result) { createPin(result); remember(result); });
    return editor;
}

void Application::openImage(const QString &path) {
    const QImage image(path);
    if (image.isNull()) { notify(t("无法打开图像：") + path, true); return; }
    createEditor(image)->show();
}

Pin *Application::createPin(const QImage &image, const QRect &region) {
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [](const auto &pin) { return pin.isNull(); }), pins_.end());
    const int maximum = qBound(1, settingInt("pin", "maxPins", 60), 200);
    while (pins_.size() >= maximum) { if (pins_.first()) pins_.first()->close(); pins_.removeFirst(); }
    QRect geometry = region;
    if (geometry.isEmpty()) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        QSize size = image.size() / screen->devicePixelRatio();
        geometry = QRect(QCursor::pos() - QPoint(size.width() / 2, size.height() / 2), size);
    }
    auto *pin = new Pin(image, geometry, settingString("save", "dir"), settingString("ocr", "language", "zh-Hans-CN"));
    pin->configure(settingBool("pin", "wheelZoom", true), settingBool("pin", "hideOnClickOutside", false), settingBool("pin", "confirmDoubleClickClose", true));
    connect(pin, &Pin::doubleClickClosePromptDisabled, this, [this] {
        settingsApply_->apply({{"pin", QJsonObject{{"confirmDoubleClickClose", false}}}});
    });
    pin->setWindowFlag(Qt::WindowStaysOnTopHint, settingBool("pin", "alwaysOnTop", true));
    pin->setWindowOpacity(qBound(0.15, settingsApply_->settings().value("pin").toObject().value("opacity").toDouble(1), 1.0));
    connect(pin, &Pin::editRequested, this, [this](Pin *source) {
        auto *editor = createEditor(source->image());
        const QPointer<Pin> guarded = source;
        connect(editor, &Editor::applyRequested, this, [this, guarded](const QImage &result) {
            if (guarded) { guarded->setImage(result); guarded->show(); }
            remember(result);
        });
        editor->show();
    });
    connect(pin, &Pin::toggleAllRequested, this, &Application::togglePins);
    connect(pin, &Pin::closeAllRequested, this, &Application::closePins);
    pins_.append(pin);
    pinsHidden_ = false;
    pin->show();
    return pin;
}

void Application::pastePin() {
    const QImage image = QApplication::clipboard()->image();
    if (image.isNull()) { notify(t("剪贴板里没有图片"), true); return; }
    createPin(image);
}

void Application::togglePins() {
    bool anyVisible = false;
    for (const auto &pin : pins_) if (pin && pin->isVisible()) anyVisible = true;
    pinsHidden_ = anyVisible;
    for (const auto &pin : pins_) if (pin) { if (anyVisible) pin->hide(); else pin->show(); }
}
void Application::closePins() { const auto pins = pins_; pins_.clear(); for (const auto &pin : pins) if (pin) pin->close(); }
void Application::applyPinSettings() {
    for (const auto &pin : pins_) if (pin) pin->configure(settingBool("pin", "wheelZoom", true), settingBool("pin", "hideOnClickOutside", false), settingBool("pin", "confirmDoubleClickClose", true));
}

void Application::startScroll(const QRect &region, const QImage &first) {
    QScreen *screen = QGuiApplication::screenAt(region.center());
    if (!screen || !screen->geometry().contains(region)) {
        finishCapture(); notify(t("滚动截图区域必须位于一块屏幕内"), true); return;
    }
    auto *hud = new ScrollCaptureUi(region, screen->availableGeometry());
    hud->setAttribute(Qt::WA_DeleteOnClose);
    if (!hud->prepare()) {
        hud->deleteLater();
        finishCapture(); notify(t("选区旁没有预览空间，请缩小选区后重试"), true); return;
    }
    hud->setImage(first);
    scrollAction_ = "edit";
    connect(hud, &ScrollCaptureUi::actionRequested, this, [this](const QString &action) { scrollAction_ = action; });
    connect(hud, &ScrollCaptureUi::finishRequested, scroll_, &ScrollSession::stop);
    const ScrollOptions options{qBound(200, settingInt("scroll", "intervalMs", 420), 5000),
        qBound(1000, settingInt("scroll", "maxHeightPx", 24000), 50000), qBound(2, settingInt("scroll", "maxFrames", 80), 300)};
    if (!scroll_->start(region, first, options)) {
        hud->deleteLater();
        finishCapture();
        notify(t("无法启动长截图：首帧为空或超过高度/内存上限"), true);
        return;
    }
    scrollHud_ = hud;
    scrollHudClosed_ = connect(hud, &QObject::destroyed, scroll_, &ScrollSession::stop);
    hud->show();
}

void Application::finishScroll(const QImage &image, const QRect &region, const QString &message) {
    disconnect(scrollHudClosed_);
    if (scrollHud_) { scrollHud_->close(); scrollHud_ = nullptr; }
    finishCapture();
    if (!image.isNull() && scrollAction_ != "cancel") route(image, region, scrollAction_);
    if (!message.isEmpty()) notify(message, true);
}

} // namespace pixclone
