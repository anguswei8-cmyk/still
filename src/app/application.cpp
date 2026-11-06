#include "ui/i18n.h"
#include "application.h"
#include "imaging/editor.h"
#include "imaging/ocr.h"
#include "imaging/ocr_result_dialog.h"
#include "capture/overlay.h"
#include "imaging/pin.h"
#include "imaging/text_pin.h"
#include "imaging/html_document.h"
#include "storage/store.h"
#include "storage/image_export.h"
#include "storage/history.h"
#include "storage/pin_session.h"
#include "settings/settings_apply.h"
#include "settings/settings_draft.h"
#include "capture/scroll_session.h"
#include "capture/scroll_capture_ui.h"
#include "capture/capture_delay.h"
#include "capture/capture_presets.h"
#include "clipboard/clipboard_controller.h"
#include "clipboard/image_copy.h"
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
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
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
#include <QtConcurrentRun>

namespace still {

static RegionScreen describeScreen(const QScreen *screen) {
    return {screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber()};
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
    regionPresetsPath_ = QDir(dataDirectory()).filePath("region-presets.json");
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
    pinSession_ = new PinSession(QDir(dataDirectory()).filePath("PinSessions"));
    history_ = new HistoryStore(initial.value("history").toObject(), this);
    settingsApply_ = new SettingsApply(initial, settingsPath(), hotkeys_, *history_, this, clipboardArchive_);
    clipboard_ = new ClipboardController(*clipboardArchive_, initial.value("clipboard").toObject(), monitorClipboard, this);
    connect(clipboard_, &ClipboardController::message, this, [this](const QString &message) { notify(message); });
    connect(clipboard_, &ClipboardController::editImage, this, [this](const QImage &image) { createEditor(image)->show(); });
    connect(clipboard_, &ClipboardController::pinImage, this, [this](const QImage &image) { createPin(image); });
    connect(clipboard_, &ClipboardController::pinText, this, [this](const QString &text, const QString &html) { createTextPin(text, html); });
    connect(clipboard_, &ClipboardController::saveImage, this, [this](const QImage &image) {
        if (saveWithDialog(nullptr, image, settingsApply_->settings().value("save").toObject())) {
            remember(image);
            if (settingBool("save", "copyAfterSave", false)) copyImage(image);
        }
    });
    connect(clipboard_, &ClipboardController::settingsRequested, this, [this] {
        openSettings();
        if (settingsWindow_) settingsWindow_->setPage(5);
    });
    connect(clipboard_, &ClipboardController::dismissRequested, this, [this] { if (settingsWindow_) settingsWindow_->hide(); });
    connect(settingsApply_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &settings, const QStringList &, const QString &) {
        if (success) {
            clipboard_->configure(settings.value("clipboard").toObject());
            InterfacePreferences::instance().apply(settings.value("appearance").toObject());
            const auto options = settings.value("save").toObject();
            for (auto *window : QApplication::topLevelWidgets()) {
                if (auto *editor = qobject_cast<Editor *>(window)) editor->configureSave(options);
                if (auto *pin = qobject_cast<Pin *>(window)) pin->configureSave(options);
            }
        }
    });
    connect(settingsApply_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &, const QStringList &, const QString &message) {
        // A pending save suppresses the page's result display, so warnings still need a notification.
        if ((!success || !message.isEmpty()) && (!settingsWindow_ || !settingsWindow_->isVisible() || settingsApply_->busy())) notify(message, true);
    });
    connect(settingsApply_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &, const QStringList &, const QString &) { if (success) applyPinSettings(); });
    connect(history_, &HistoryStore::errorOccurred, this, [this](const QString &message) { notify(message, true); });
    const QIcon appIcon(":/brand/still.png");
    QApplication::setWindowIcon(appIcon);
#ifdef Q_OS_MACOS
    QIcon trayIcon(":/brand/still-tray.png");
    trayIcon.setIsMask(true);
    tray_.setIcon(trayIcon);
#else
    tray_.setIcon(appIcon);
#endif
    tray_.setToolTip(t("拾影 · 截图与剪贴板"));
    auto *menu = new QMenu;
    menu->setObjectName("TrayMenu");
    menu->addAction(t("截图并编辑"), this, [this] { capture(); });
    menu->addAction(t("剪贴板历史…"), this, &Application::openClipboard);
    auto *pinMenu = menu->addMenu(t("贴图操作"));
    pinMenu->setObjectName("TrayPinMenu");
    pinMenu->addAction(t("剪贴板内容贴图"), this, &Application::pastePin);
    pinMenu->addAction(t("显示/隐藏全部贴图"), this, &Application::togglePins);
    restoreClosedAction_ = pinMenu->addAction(t("恢复最近关闭贴图"), this, &Application::restoreClosedPin);
    restoreClosedAction_->setObjectName("RestoreClosedPin");
    restoreClosedAction_->setEnabled(false);
    auto *restoreSession = pinMenu->addAction(t("恢复上次贴图会话"), this, &Application::restorePinSession);
    restoreSession->setObjectName("RestorePinSession");
    pinMenu->addSeparator();
    pinMenu->addAction(t("关闭全部贴图"), this, &Application::closePins);
    auto *more = menu->addMenu(t("更多操作"));
    more->setObjectName("TrayMoreMenu");
    more->addAction(t("快速截图复制"), this, [this] { capture("copy"); });
    auto *delayedCapture = more->addAction(t("延时截图…"), this, &Application::openDelayedCapture);
    delayedCapture->setObjectName("DelayedCapture");
    repeatCaptureAction_ = more->addAction(t("重复上次区域"), this, [this] { capture("repeat"); });
    repeatCaptureAction_->setObjectName("RepeatCapture"); repeatCaptureAction_->setEnabled(false);
    auto *regionPresets = more->addAction(t("区域预设…"), this, &Application::openCapturePresets);
    regionPresets->setObjectName("RegionPresets");
    more->addAction(t("整屏截图并贴图"), this, [this] { capture("fullScreen"); });
    more->addAction(t("滚动长截图"), this, [this] { capture("scroll"); });
    more->addAction(t("打开图片编辑…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(nullptr, t("打开图片"), {}, t("图像 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (!path.isEmpty()) openImage(path);
    });
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
        const QImage frame = captureScreen(screen);
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
    qApp->installEventFilter(this);
    QTimer::singleShot(0, this, [this] {
        openClipboard();
        if (settingBool("pin", "restoreOnStartup", false)) restorePinSession();
    });
}

Application::~Application() {
    qApp->removeEventFilter(this);
    delete captureDelay_.data();
    delete capturePresets_.data();
    if (pinRestoreJob_) pinRestoreJob_->future().waitForFinished();
    if (pinSessionTouched_ && !pinSessionIncomplete_) {
        QVector<PinState> states = exitingPins_;
        if (!quitting_) for (const auto &pin : pins_) if (pin) states.append(pin->state());
        // Commit one complete session on normal shutdown; never encode screenshots during a drag.
        const QString error = QtConcurrent::run([session = pinSession_, states] {
            QString error;
            session->save(states, &error);
            return error;
        }).result();
        if (!error.isEmpty()) qWarning().noquote() << t("贴图会话保存失败，原会话已保留：") << error;
    }
    for (const auto &pin : pins_) delete pin.data();
    pins_.clear();
    delete pinSession_;
    disconnect(scrollHudClosed_);
    if (scroll_) { scroll_->disconnect(this); scroll_->stop(); }
    delete scrollHud_.data();
    if (settingsWindow_) settingsWindow_->commitDrafts();
    delete settingsWindow_.data();
    // Finish settings while their dependent controllers and stores are still alive.
    delete settingsApply_;
    delete clipboard_;
    delete scroll_;
    delete history_;
    delete clipboardArchive_;
}

bool Application::eventFilter(QObject *object, QEvent *event) {
    if (object == qApp && event->type() == QEvent::Quit && !quitting_) {
        // QApplication sends Close to widgets while handling Quit; capture the session first.
        quitting_ = true;
        exitingPins_.clear();
        for (const auto &pin : pins_) if (pin) exitingPins_.append(pin->state());
    }
    return QObject::eventFilter(object, event);
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
    settingsWindow_->showAndActivate();
}

void Application::notify(const QString &message, bool error) {
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_.showMessage(t("拾影"), message, error ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 3500);
    else if (error) QMessageBox::warning(settingsWindow_, t("拾影"), message);
}

void Application::openDelayedCapture() {
    if (capturing_ || scroll_->active()) return;
    if (!captureDelay_) {
        auto *dialog = new CaptureDelay;
        captureDelay_ = dialog;
        connect(dialog, &QDialog::finished, this, [this](int result) {
            captureDelay_.clear();
            if (result == QDialog::Accepted) capture();
        });
    }
    captureDelay_->show(); captureDelay_->raise(); captureDelay_->activateWindow();
}

void Application::openCapturePresets() {
    if (capturing_ || scroll_->active()) return;
    if (!capturePresets_) {
        auto *dialog = new CapturePresets(regionPresetsPath_);
        capturePresets_ = dialog;
        connect(dialog, &CapturePresets::captureRequested, this, [this](const RegionPreset &preset) { beginCapture("annotate", preset); });
        connect(dialog, &QDialog::finished, this, [this] { capturePresets_.clear(); });
    }
    capturePresets_->setCurrentRegion(lastCaptureRegion_);
    capturePresets_->show(); capturePresets_->raise(); capturePresets_->activateWindow();
}

void Application::rememberCaptureRegion(QScreen *screen, const RegionScreen &identity, const QRectF &region) {
    lastCaptureScreen_ = screen; lastCaptureRegion_ = {{}, identity, region};
    repeatCaptureAction_->setEnabled(true);
    if (capturePresets_) capturePresets_->setCurrentRegion(lastCaptureRegion_);
}

void Application::capture(const QString &mode) { beginCapture(mode); }

void Application::beginCapture(const QString &mode, std::optional<RegionPreset> preset) {
    if (capturing_ || scroll_->active()) return;
    if (captureDelay_) {
        const auto pending = captureDelay_; captureDelay_.clear(); pending->reject();
    }
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
    QTimer::singleShot(180, this, [this, mode, preset] {
        QVector<CaptureFrame> frames;
        for (QScreen *screen : QGuiApplication::screens()) {
            QImage image = captureScreen(screen);
            if (image.isNull()) continue;
            image.setDevicePixelRatio(1);
            const int maxEdge = settingInt("capture", "maxEdge", 0);
            if (maxEdge > 0 && qMax(image.width(), image.height()) > maxEdge)
                image = image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            frames.append({image, screen->geometry(), settingBool("capture", "snapToWindows", true) ? windowRectangles(screen) : QVector<QRect>(), screen, describeScreen(screen)});
        }
        if (frames.isEmpty()) { finishCapture(); notify(t("截图失败，请检查屏幕录制权限"), true); return; }
        if (mode == "fullScreen") {
            finishCapture();
            for (const auto &frame : frames) { createPin(frame.image, frame.geometry); remember(frame.image); }
            return;
        }
        const QString action = mode == "annotate" || mode == "repeat" ? settingString("capture", "afterSelectAction", "annotate") : mode;
        if (mode == "repeat" || preset) {
            QPointer<QScreen> wanted = lastCaptureScreen_;
            const QRectF region = preset ? preset->region : lastCaptureRegion_.region;
            if (preset) {
                const auto screens = QGuiApplication::screens(); QVector<RegionScreen> identities;
                for (const auto *screen : screens) identities.append(describeScreen(screen));
                const int matched = matchingPresetScreen(preset->screen, identities);
                wanted = matched < 0 ? nullptr : screens[matched];
            }
            const auto frame = std::find_if(frames.cbegin(), frames.cend(), [wanted](const CaptureFrame &frame) { return wanted && frame.screen == wanted; });
            if (frame != frames.cend() && QRectF(QPointF(), frame->geometry.size()).contains(region)) {
                if (preset) rememberCaptureRegion(frame->screen, frame->screenInfo, region);
                deliverCapture(cropFrame(frame->image, frame->geometry.size(), region),
                               region.translated(frame->geometry.topLeft()).toAlignedRect(), action);
                return;
            }
            QMessageBox::information(settingsWindow_, t("重新选择区域"), t("原屏幕或区域不可用，请重新框选。"));
        }
        for (auto &frame : frames) {
            auto *overlay = new Overlay(frame, action, settingBool("capture", "showMagnifier", true));
            overlays_.append(overlay);
            connect(overlay, &Overlay::accepted, this, [this, screen = frame.screen, identity = frame.screenInfo, origin = frame.geometry.topLeft()](const QImage &image, const QRectF &localRegion, const QString &action) {
                rememberCaptureRegion(screen, identity, localRegion);
                deliverCapture(image, localRegion.translated(origin).toAlignedRect(), action);
            });
            connect(overlay, &Overlay::cancelled, this, &Application::finishCapture);
            overlay->show();
        }
        for (const auto &overlay : overlays_) {
            if (overlay && overlay->geometry().contains(QCursor::pos())) { overlay->raise(); overlay->activateWindow(); overlay->setFocus(); break; }
        }
    });
}

void Application::deliverCapture(QImage image, QRect region, QString action) {
    // Own the values before closing the emitting overlay and freeing its frozen frame.
    closeOverlays();
    QTimer::singleShot(0, this, [this, image, region, action] {
        if (action == "scroll") startScroll(region, image);
        else { finishCapture(); route(image, region, action); }
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

bool Application::copyImage(const QImage &image) {
    QString error;
    if (copyImageToClipboard(image, settingsApply_->settings().value("save").toObject(), &error)) return true;
    notify(t("复制失败：") + error, true);
    return false;
}

void Application::route(const QImage &image, const QRect &region, const QString &action) {
    if (action == "copy") {
        if (copyImage(image)) { remember(image); notify(settingBool("save", "copyAsFile", false) ? t("已复制图像文件") : t("已复制截图")); }
        else createEditor(image)->show();
    }
    else if (action == "save" || action == "quickSave") {
        const auto options = settingsApply_->settings().value("save").toObject();
        bool saved;
        if (action == "quickSave") {
            QString error;
            const auto path = exportImage(image, options, &error);
            saved = !path.isEmpty();
            notify(saved ? t("已保存到：") + path : t("保存失败") + ": " + error, !saved);
            if (!saved) createEditor(image)->show();
        } else saved = saveWithDialog(settingsWindow_, image, options);
        if (saved) { remember(image); if (settingBool("save", "copyAfterSave", false)) copyImage(image); }
    }
    else if (action == "pin") { createPin(image, region); remember(image); }
    else if (action == "ocr") {
        delete ocrJob_.data();
        auto *job = new OcrJob(image, settingString("ocr", "language", "zh-Hans-CN"), this);
        ocrJob_ = job;
        connect(job, &OcrJob::finished, this, [this](const OcrResult &result) {
            const QString &text = result.text;
            if (!result.error.isEmpty() || text.isEmpty()) { notify(result.error.isEmpty() ? t("未识别到文字") : result.error, true); return; }
            QApplication::clipboard()->setText(text);
            notify(t("已复制识别文字"));
            auto *dialog = new OcrResultDialog(text);
            connect(this, &QObject::destroyed, dialog, &QWidget::close);
            dialog->show();
        });
    } else createEditor(image)->show();
}

Editor *Application::createEditor(const QImage &image, Pin *target) {
    auto *editor = new Editor(image, settingsApply_->settings().value("save").toObject(), settingString("ocr", "language", "zh-Hans-CN"), target != nullptr);
    connect(editor, &Editor::imageCopied, this, &Application::remember);
    connect(editor, &Editor::imageSaved, this, [this](const QImage &result) {
        remember(result);
        if (settingBool("save", "copyAfterSave", false)) copyImage(result);
    });
    connect(editor, &Editor::pinRequested, this, [this](const QImage &result) { createPin(result); remember(result); });
    if (target) {
        connect(editor, &Editor::applyRequested, this, [this, guarded = QPointer<Pin>(target)](const QImage &result) {
            if (guarded) { guarded->setImage(result); guarded->show(); }
            remember(result);
        });
    }
    return editor;
}

void Application::openImage(const QString &path) {
    const QImage image(path);
    if (image.isNull()) { notify(t("无法打开图像：") + path, true); return; }
    createEditor(image)->show();
}

Pin *Application::createPin(const QImage &image, const QRect &region, const PinState *restored) {
    QRect geometry = region;
    if (geometry.isEmpty()) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        QSize size = image.size() / screen->devicePixelRatio();
        geometry = QRect(QCursor::pos() - QPoint(size.width() / 2, size.height() / 2), size);
    }
    auto *pin = new Pin(image, geometry, settingsApply_->settings().value("save").toObject(), settingString("ocr", "language", "zh-Hans-CN"));
    connect(pin, &Pin::imageSaved, this, [this](const QImage &result) {
        remember(result);
        if (settingBool("save", "copyAfterSave", false)) copyImage(result);
    });
    pin->configure(settingBool("pin", "wheelZoom", true), settingBool("pin", "hideOnClickOutside", false), settingBool("pin", "confirmDoubleClickClose", true));
    connect(pin, &Pin::doubleClickClosePromptDisabled, this, [this] {
        settingsApply_->apply({{"pin", QJsonObject{{"confirmDoubleClickClose", false}}}});
    });
    connect(pin, &Pin::editRequested, this, [this](Pin *source) {
        createEditor(source->image(), source)->show();
    });
    attachPin(pin, restored);
    return pin;
}

TextPin *Application::createTextPin(const QString &text, const QString &html, const PinState *restored) {
    auto *pin = new TextPin(text, html);
    attachPin(pin, restored);
    return pin;
}

void Application::attachPin(PinWindow *pin, const PinState *restored) {
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [](const auto &entry) { return entry.isNull(); }), pins_.end());
    const int maximum = qBound(1, settingInt("pin", "maxPins", 60), 200);
    while (pins_.size() >= maximum) pins_.first()->close();
    pin->setHideOnBlur(settingBool("pin", "hideOnClickOutside", false));
    pin->setWindowFlag(Qt::WindowStaysOnTopHint, settingBool("pin", "alwaysOnTop", true));
    pin->setWindowOpacity(qBound(0.15, settingsApply_->settings().value("pin").toObject().value("opacity").toDouble(1), 1.0));
    if (restored) pin->restoreLayout(*restored);
    connect(pin, &PinWindow::closing, this, [this, pin](const PinState &state) {
        pins_.removeAll(pin);
        if (quitting_) return;
        pinSession_->rememberClosed(state);
        pinSessionTouched_ = true;
        updatePinRestoreActions();
    }, Qt::SingleShotConnection);
    connect(pin, &PinWindow::toggleAllRequested, this, &Application::togglePins);
    connect(pin, &PinWindow::closeAllRequested, this, &Application::closePins);
    connect(pin, &PinWindow::restoreClosedRequested, this, &Application::restoreClosedPin);
    pins_.append(pin);
    pinSessionTouched_ = true;
    updatePinRestoreActions();
    pinsHidden_ = false;
    pin->show();
}

void Application::pastePin() {
    const QImage image = QApplication::clipboard()->image();
    if (!image.isNull()) { createPin(image); return; }
    const auto *mime = QApplication::clipboard()->mimeData();
    const auto urls = mime ? mime->urls() : QList<QUrl>();
    const bool files = std::any_of(urls.cbegin(), urls.cend(), [](const QUrl &url) { return url.isLocalFile(); });
    if (mime && (mime->hasText() || mime->hasHtml()) && !files) {
        const auto html = mime->hasHtml() ? mime->html() : QString();
        if (html.toUtf8().size() > maximumTextPinBytes) { notify(t("文本贴图内容无效或超过 32 MiB"), true); return; }
        auto text = mime->text();
        if (text.isEmpty() && !html.isEmpty()) text = SafeHtmlDocument(html).toPlainText();
        if (!text.isEmpty() && text.toUtf8().size() + html.toUtf8().size() <= maximumTextPinBytes) { createTextPin(text, html); return; }
        notify(t("文本贴图内容无效或超过 32 MiB"), true);
        return;
    }
    notify(t("剪贴板里没有可贴出的图片或文字"), true);
}

void Application::togglePins() {
    bool anyVisible = false;
    for (const auto &pin : pins_) if (pin && pin->isVisible()) anyVisible = true;
    pinsHidden_ = anyVisible;
    for (const auto &pin : pins_) if (pin) { if (anyVisible) pin->hide(); else pin->show(); }
}
void Application::closePins() { const auto pins = pins_; for (const auto &pin : pins) if (pin) pin->close(); }

void Application::updatePinRestoreActions() {
    const bool available = pinSession_->canRestoreClosed();
    restoreClosedAction_->setEnabled(available);
    for (const auto &pin : pins_) if (pin) pin->setRestoreAvailable(available);
}

void Application::restoreClosedPin() {
    const auto state = pinSession_->takeClosed();
    if (state) {
        if (!state->image.isNull()) createPin(state->image, state->geometry, &*state);
        else createTextPin(state->text, state->html, &*state);
    }
    updatePinRestoreActions();
}

void Application::restorePinSession() {
    if (pinRestoreJob_) return;
    auto *job = new QFutureWatcher<PinSessionResult>(this);
    job->setObjectName("PinSessionRestoreJob");
    pinRestoreJob_ = job;
    connect(job, &QFutureWatcher<PinSessionResult>::finished, this, [this, job] {
        if (quitting_) { pinRestoreJob_ = nullptr; job->deleteLater(); return; }
        const auto result = job->result();
        const int maximum = qBound(1, settingInt("pin", "maxPins", 60), 200);
        bool limited = result.limited;
        for (const auto &state : result.pins) {
            bool open = false;
            for (const auto &pin : pins_) if (pin && pin->state().id == state.id) open = true;
            if (open) continue;
            if (pins_.size() >= maximum) { limited = true; break; }
            if (!state.image.isNull()) createPin(state.image, state.geometry, &state);
            else createTextPin(state.text, state.html, &state);
        }
        pinSessionIncomplete_ = !result.errors.isEmpty() || limited;
        if (!result.errors.isEmpty()) {
            QString message = result.errors.mid(0, 3).join('\n');
            if (result.errors.size() > 3) message += '\n' + t("另有 %1 张贴图无法恢复").arg(result.errors.size() - 3);
            notify(t("部分贴图无法恢复，本次退出将保留原会话：") + message, true);
        }
        if (limited) notify(t("已达到贴图数量上限，部分贴图尚未恢复，原会话将保留"), true);
        pinRestoreJob_ = nullptr;
        job->deleteLater();
    });
    QStringList open;
    for (const auto &pin : pins_) if (pin) open.append(pin->state().id);
    const int available = qMax(0, qBound(1, settingInt("pin", "maxPins", 60), 200) - int(open.size()));
    job->setFuture(QtConcurrent::run([session = pinSession_, open, available] { return session->load(open, available); }));
}
void Application::applyPinSettings() {
    for (const auto &pin : pins_) if (pin) {
        pin->setHideOnBlur(settingBool("pin", "hideOnClickOutside", false));
        if (auto *image = qobject_cast<Pin *>(pin.data())) image->configure(settingBool("pin", "wheelZoom", true), settingBool("pin", "hideOnClickOutside", false), settingBool("pin", "confirmDoubleClickClose", true));
    }
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

} // namespace still
