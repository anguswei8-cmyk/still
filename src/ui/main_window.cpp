#include "main_window.h"
#include "clipboard/clipboard_archive.h"
#include "clipboard/clipboard_controller.h"
#include "settings/settings_apply.h"
#include "settings/settings_backup_dialog.h"
#include "settings/api_settings_dialog.h"
#include "storage/image_export.h"
#include "toolicons.h"
#include "i18n.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHideEvent>
#include <QKeySequence>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QShowEvent>
#include <QStyle>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace still {
// The provider keeps only thumbnails and one selected preview, never all original images.
class UiImages final : public QQuickImageProvider {
public:
    UiImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    void insert(const QString &id, const QImage &image) { QMutexLocker lock(&mutex_); images_.insert(id, image); }
    void replace(const QString &prefix, const QHash<QString, QImage> &images) {
        QMutexLocker lock(&mutex_);
        for (auto it = images_.begin(); it != images_.end();) { if (it.key().startsWith(prefix)) it = images_.erase(it); else ++it; }
        images_.insert(images);
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        QImage image;
        { QMutexLocker lock(&mutex_); image = images_.value(id.section('?', 0, 0)); if (image.isNull()) image = images_.value("icon/history"); }
        if (size) *size = image.size();
        return requested.isValid() ? image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
    }
private:
    QMutex mutex_;
    QHash<QString, QImage> images_;
};

MainWindow::MainWindow(SettingsApply &settings, HistoryStore &history, ClipboardArchive &archive, ClipboardController &clipboard)
    : settings_(settings), history_(history), archive_(archive), clipboard_(clipboard), draft_(settings), browse_(archive) {
    setObjectName("SettingsShell"); setWindowTitle(t("拾影"));
    refreshStartup();
    setAttribute(Qt::WA_DeleteOnClose); setMinimumSize(800, 620); resize(1080, 740);
    setWindowFlags(Qt::Window | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MACOS
    // QML reserves the traffic-light area; do not inset the entire content surface.
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#endif
#ifdef Q_OS_WIN
    setWindowFlag(Qt::FramelessWindowHint);
    if (QGuiApplication::platformName() == "windows") setAttribute(Qt::WA_TranslucentBackground);
#endif
    images_ = new UiImages;
    QHash<QString, QImage> icons;
    const QList<QPair<QString, ToolIcon>> glyphs{{"clipboard", ToolIcon::Clipboard}, {"general", ToolIcon::Settings}, {"hotkeys", ToolIcon::Keyboard},
        {"history", ToolIcon::Image}, {"advanced", ToolIcon::Ocr}, {"settings", ToolIcon::Settings}, {"capture", ToolIcon::Capture},
        {"copy", ToolIcon::Copy}, {"star", ToolIcon::Star}, {"more", ToolIcon::Ellipsis}, {"pause", ToolIcon::Pause}, {"close", ToolIcon::Close}, {"edit", ToolIcon::Edit}, {"pin", ToolIcon::Pin},
        {"search", ToolIcon::Search}, {"trash", ToolIcon::Trash}, {"chevron", ToolIcon::ChevronDown}, {"undo", ToolIcon::Undo},
        {"minimize", ToolIcon::Minimize}, {"maximize", ToolIcon::Maximize}, {"restore", ToolIcon::Restore}, {"back", ToolIcon::Back},
        {"text", ToolIcon::TextSnippet}, {"rich", ToolIcon::TextSnippet}, {"files", ToolIcon::Files}, {"link", ToolIcon::Link}, {"image", ToolIcon::Image}, {"data", ToolIcon::Clipboard}};
    for (const auto &glyph : glyphs) for (bool active : {false, true}) for (bool dark : {false, true})
        icons.insert("icon/" + glyph.first + (active ? "-active" : "") + (dark ? "-dark" : ""),
            toolIcon(glyph.second, QColor(dark ? (active ? "#dedede" : "#adadad") : (active ? "#454b57" : "#626875"))).pixmap(40, 40).toImage());
    for (const auto &glyph : glyphs) icons.insert("icon/" + glyph.first + "-inverse", toolIcon(glyph.second, Qt::white).pixmap(40, 40).toImage());
    images_->replace("icon/", icons);
    view_ = new QQuickWidget(this); view_->setObjectName("QmlView"); view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    QSurfaceFormat format = view_->format(); format.setAlphaBufferSize(8); view_->setFormat(format);
    view_->setClearColor(Qt::transparent);
    view_->engine()->addImageProvider("content", images_);
    view_->rootContext()->setContextProperty("backend", this);
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, [this] {
        startupError_ = InterfacePreferences::instance().translatedCaption(startupError_);
        refreshStartup();
        updateBackdrop();
        view_->engine()->retranslate();
        setWindowTitle(t("拾影"));
        emit appearanceChanged(); emit pageChanged();
        if (page_ == 3) refreshHistory();
    });
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->addWidget(view_);
    clipboard_.attachHost(this);
    connect(&clipboard_, &ClipboardController::stateChanged, this, &MainWindow::clipboardStateChanged);
    connect(&clipboard_, &ClipboardController::feedback, this, &MainWindow::showMessage);
    connect(&browse_, &ClipboardBrowse::message, this, &MainWindow::showMessage);
    connect(&browse_, &ClipboardBrowse::searchChanged, this, &MainWindow::searchChanged);
    connect(&browse_, &ClipboardBrowse::filterChanged, this, &MainWindow::filterChanged);
    connect(&browse_, &ClipboardBrowse::selectionChanged, this, &MainWindow::selectionChanged);
    connect(&browse_, &ClipboardBrowse::clipsChanged, this, [this] {
        images_->replace("clip/", browse_.thumbnails()); emit clipsChanged();
    });
    connect(&browse_, &ClipboardBrowse::previewChanged, this, [this] {
        images_->replace("preview/", {{"preview/current", browse_.previewImage()}}); emit previewChanged();
    });
    connect(&history_, &HistoryStore::changed, this, [this] { if (page_ == 3) refreshHistory(); });
    connect(&draft_, &SettingsDraft::formChanged, this, &MainWindow::formChanged);
    connect(&draft_, &SettingsDraft::statusChanged, this, &MainWindow::statusChanged);
    images_->replace("clip/", browse_.thumbnails());
    view_->setSource(QUrl("qrc:/qt/qml/Still/Main.qml"));
    if (view_->status() == QQuickWidget::Error) for (const auto &error : view_->errors()) qCritical().noquote() << error.toString();
}

MainWindow::~MainWindow() {
    historyThumbnails_.cancel();
    browse_.setActive(false); clipboard_.attachHost(nullptr);
    delete view_; view_ = nullptr; images_ = nullptr;
}
QVariantList MainWindow::settingRows() const { return SettingsDraft::rows(page_); }
QString MainWindow::saveNamePreview() const { return imageExportFileName(draft_.form().value("save").toObject()); }
bool MainWindow::busy() const { return settings_.busy(); }
QVariantMap MainWindow::startup() const {
    const bool needsAttention = startup_.state == StartupState::RequiresApproval || startup_.state == StartupState::NotFound;
    return {{"enabled", startup_.state == StartupState::Enabled},
            {"canToggle", startup_.state == StartupState::Disabled || startup_.state == StartupState::Enabled || startup_.state == StartupState::NotFound},
            {"needsAttention", needsAttention},
            {"showSettings", needsAttention || !startup_.settingsLabel.isEmpty()},
            {"settingsLabel", startup_.settingsLabel.isEmpty() ? t("系统登录项…") : startup_.settingsLabel},
            {"message", startup_.message}, {"error", startupError_}};
}
void MainWindow::refreshStartup() {
    const auto current = startupStatus();
    if (current.state != startup_.state) startupError_.clear();
    startup_ = current;
    emit startupChanged();
}
void MainWindow::setStartupEnabled(bool enabled) {
    QString error;
    const bool success = still::setStartupEnabled(enabled, &error);
    startup_ = startupStatus();
    startupError_ = success ? QString() : error;
    emit startupChanged();
}
void MainWindow::openStartupSettings() { still::openStartupSettings(); }
bool MainWindow::dark() const { return InterfacePreferences::instance().dark(); }
bool MainWindow::moveWindow() { return windowHandle() && windowHandle()->startSystemMove(); }
bool MainWindow::resizeWindow(int edges) { return !isMaximized() && windowHandle() && windowHandle()->startSystemResize(Qt::Edges(edges)); }
void MainWindow::showAndActivate() {
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
#ifdef Q_OS_MACOS
    [NSApp activateIgnoringOtherApps:YES];
#endif
    raise(); activateWindow();
}
void MainWindow::toggleMaximized() { isMaximized() ? showNormal() : showMaximized(); }
void MainWindow::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) emit windowStateChanged();
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) refreshStartup();
}
int MainWindow::totalCount() const { return archive_.entries().size(); }
bool MainWindow::paused() const { return clipboard_.paused(); }
bool MainWindow::pasteDefault() const { return clipboard_.pasteDefault(); }
void MainWindow::setPage(int page) {
    if (page < 0 || page > 5 || page == page_) return;
    commitDrafts(); clipboard_.cancel();
    if (page_ == 3) { ++historyGeneration_; ++historyOpenGeneration_; historyThumbnails_.cancel(); }
    page_ = page; showMessage({}); emit pageChanged(); browse_.setActive(page_ == 0 && isVisible());
    if (page == 3) refreshHistory();
    if (page == 1) refreshStartup();
}
void MainWindow::focusSearch() { setPage(0); view_->setFocus(); emit focusSearchRequested(); }
void MainWindow::showMessage(const QString &message) { message_ = message; emit messageChanged(); }

void MainWindow::setShortcut(const QString &name, int key, int modifiers) {
    if (!hotkeyNames.contains(name) || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_unknown) return;
    const QString sequence = key == Qt::Key_Backspace || key == Qt::Key_Delete ? QString() : QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(QKeySequence::PortableText);
    setSetting("hotkeys", name, sequence);
}

bool MainWindow::restoreDefault(const QString &group, const QString &key) {
    QVariantMap field;
    for (int page = 1; page <= 5; ++page) for (const auto &value : SettingsDraft::rows(page)) {
        const auto row = value.toMap();
        if (!key.isEmpty() && row.value("group") == group && row.value("key") == key) field = row;
    }
    if (field.isEmpty()) return false;
    const auto expected = QJsonValue::fromVariant(field.value("defaultValue"));
    const auto saved = settings_.settings().value(group).toObject().value(key);
    const auto draft = draft_.form().value(group).toObject().value(key);
    QString text;
    if ((group == "history" || group == "clipboard") && key == "dir" && (saved != expected || draft != expected))
        text = t("将“%1”恢复到应用默认目录，并迁移已有历史？迁移失败时保留原设置和数据。").arg(field.value("title").toString());
    else if ((group == "history" || group == "clipboard") && (key == "maxItems" || key == "maxMiB")
             && (expected.toDouble() < saved.toDouble() || expected.toDouble() < draft.toDouble())) {
        const QString unit = field.value("unit").toString();
        const QString value = field.value("defaultValue").toString() + (unit.isEmpty() ? QString() : ' ' + unit);
        text = t("将“%1”恢复为 %2？降低配额可能清理旧内容。").arg(field.value("title").toString(), value);
    }
    if (!text.isEmpty()) {
        QMessageBox question(QMessageBox::Question, t("恢复单项默认"), text, QMessageBox::Yes | QMessageBox::No, this);
        question.setObjectName("RestoreSettingConfirmation"); question.setTextFormat(Qt::PlainText); question.setDefaultButton(QMessageBox::No);
        auto *style = QStyleFactory::create("Fusion"); style->setParent(&question); question.setStyle(style);
        for (auto *widget : question.findChildren<QWidget *>()) widget->setStyle(style);
        if (question.exec() != QMessageBox::Yes) return false;
    }
    draft_.restoreDefault(group, key);
    return true;
}
QString MainWindow::shortcutText(const QString &name) const { return QKeySequence::fromString(draft_.form().value("hotkeys").toObject().value(name).toString(), QKeySequence::PortableText).toString(QKeySequence::NativeText); }
void MainWindow::browseDirectory(const QString &group) {
    if (group != "save" && group != "history" && group != "clipboard") return;
    const QString path = QFileDialog::getExistingDirectory(this, group == "save" ? t("选择保存目录") : t("选择专用空文件夹，迁移已有历史"), draft_.form().value(group).toObject().value("dir").toString());
    if (!path.isEmpty()) setSetting(group, "dir", path);
}
void MainWindow::openDirectory(const QString &group) {
    const QString path = group == "history" ? history_.directory() : group == "clipboard" ? archive_.directory() : QString();
    if (!path.isEmpty()) { QDir().mkpath(path); QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
}

void MainWindow::openApiSettings() { (new ApiSettingsDialog(this))->open(); }

void MainWindow::openSettingsBackup() {
    commitDrafts();
    SettingsBackupDialog dialog(settings_, this);
    connect(&dialog, &SettingsBackupDialog::settingsRestored, &draft_, &SettingsDraft::reloadSaved);
    dialog.exec();
}

void MainWindow::setSelectedIndex(int index) {
    if (browse_.setSelectedIndex(index)) clipboard_.cancel();
}
void MainWindow::activate(const QString &action) {
    if (browse_.selectedId().isEmpty()) return;
    const QString kind = selected().value("kind").toString();
    ClipAction requested = clipboard_.pasteDefault() ? ClipAction::Paste : ClipAction::Copy;
    if (action == "copy") requested = ClipAction::Copy;
    else if (action == "plain") requested = ClipAction::PlainPaste;
    else if (action == "edit") requested = ClipAction::EditImage;
    else if (action == "pin") {
        if (kind == "image") requested = ClipAction::PinImage;
        else if (kind == "text" || kind == "rich" || kind == "link") requested = ClipAction::PinText;
        else return;
    }
    else if (action == "save") requested = ClipAction::SaveImage;
    else if (!action.isEmpty()) return;
    if ((action == "edit" || action == "save") && kind != "image") return;
    clipboard_.activate(browse_.selectedId(), requested);
}
void MainWindow::togglePin() { if (!browse_.selectedId().isEmpty()) archive_.pin(browse_.selectedId(), !selected().value("pinned").toBool()); }
void MainWindow::removeSelected() { if (!browse_.selectedId().isEmpty()) archive_.remove(browse_.selectedId()); }
void MainWindow::clearClipboard() {
    QMessageBox question(QMessageBox::Question, t("清空剪贴板历史"), t("删除本地保存的剪贴板内容？文件条目不会删除原文件。"), QMessageBox::Yes | QMessageBox::No, this);
    auto *includePinned = new QCheckBox(t("同时删除已固定的内容")); question.setCheckBox(includePinned);
    if (question.exec() == QMessageBox::Yes) archive_.clear(includePinned->isChecked());
}
void MainWindow::togglePause() { clipboard_.setPaused(!clipboard_.paused()); }

void MainWindow::refreshHistory() {
    const auto generation = ++historyGeneration_;
    historyThumbnails_.cancel();
    historyItems_.clear(); historyEntries_.clear(); images_->replace("history/", {});
    historyLoading_ = true; historySummary_ = t("正在读取历史…"); emit historyChanged();
    auto *watcher = new QFutureWatcher<HistoryListing>(this);
    connect(watcher, &QFutureWatcher<HistoryListing>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != historyGeneration_) return;
        const auto result = watcher->result();
        historyEntries_ = result.entries; historyLoading_ = false;
        historySummary_ = t("%1 张截图，占用 %2 MiB").arg(result.entries.size()).arg(result.bytes / 1048576.0, 0, 'f', 1);
        emit historyChanged(); loadMoreHistory();
    });
    watcher->setFuture(history_.list());
}
void MainWindow::loadMoreHistory() {
    if (historyLoading_ || !historyMore()) return;
    historyLoading_ = true; emit historyChanged();
    const auto generation = historyGeneration_;
    const auto entries = historyEntries_.mid(historyItems_.size(), 50);
    QStringList paths;
    for (const auto &entry : entries) paths.append(entry.path);
    using Thumbnails = QList<QImage>;
    auto *watcher = new QFutureWatcher<Thumbnails>(this);
    connect(watcher, &QFutureWatcher<Thumbnails>::finished, this, [this, watcher, generation, entries] {
        watcher->deleteLater();
        if (generation != historyGeneration_ || watcher->isCanceled()) return;
        const auto result = watcher->result();
        for (qsizetype i = 0; i < result.size(); ++i) {
            const auto &entry = entries[i];
            const QString key = QStringLiteral("history/%1/%2").arg(generation).arg(historyItems_.size());
            images_->insert(key, result[i]);
            historyItems_.append(QVariantMap{{"path", entry.path}, {"title", entry.name}, {"date", entry.modified.toString("yyyy-MM-dd HH:mm")}, {"image", "image://content/" + key}});
        }
        historyLoading_ = false; emit historyChanged();
    });
    historyThumbnails_ = history_.thumbnails(std::move(paths), QSize(224, 152));
    watcher->setFuture(historyThumbnails_);
}
void MainWindow::historyAction(const QString &path, const QString &action) {
    if (busy() || std::none_of(historyEntries_.cbegin(), historyEntries_.cend(), [&](const HistoryEntry &entry) { return entry.path == path; })) return;
    if (action == "remove") {
        // Keep the path by value: confirmation runs an event loop that may refresh the model.
        const QString selected = path;
        if (QMessageBox::question(this, t("删除历史截图"), t("删除选中的截图文件？此操作无法撤销。")) == QMessageBox::Yes) history_.remove(selected);
    } else if ((action == "edit" || action == "pin") && !historyOpening_) {
        historyOpening_ = true;
        const auto generation = historyOpenGeneration_;
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation, pin = action == "pin"] {
            watcher->deleteLater();
            historyOpening_ = false;
            if (generation != historyOpenGeneration_ || page_ != 3 || !isVisible()) return;
            const auto image = watcher->result();
            if (image.isNull()) showMessage(t("历史图片已被删除或无法读取"));
            else emit historyImageRequested(image, pin);
        });
        watcher->setFuture(history_.readImage(path));
    }
}
void MainWindow::clearHistory() {
    if (!busy() && QMessageBox::question(this, t("清空截图历史"), t("删除全部历史截图文件？此操作无法撤销。")) == QMessageBox::Yes) history_.clear();
}
void MainWindow::closeEvent(QCloseEvent *event) { commitDrafts(); QDialog::closeEvent(event); }
void MainWindow::reject() { commitDrafts(); QDialog::reject(); }
void MainWindow::hideEvent(QHideEvent *event) {
    ++historyOpenGeneration_;
    browse_.setActive(false);
    QDialog::hideEvent(event);
#ifdef Q_OS_MACOS
    // Native minimization keeps the window logically visible and its Dock entry.
    if (!isVisible()) [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
#endif
}
void MainWindow::updateBackdrop() {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != "windows") return;
    HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW), 0, nullptr};
    SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    const auto handle = reinterpret_cast<HWND>(winId());
    const BOOL night = dark();
    DwmSetWindowAttribute(handle, 20, &night, sizeof(night));
    // Numeric IDs support older MinGW headers; unsupported OSes use opaque QML.
    const int acrylic = contrast.dwFlags & HCF_HIGHCONTRASTON ? 1 : 3;
    const bool applied = SUCCEEDED(DwmSetWindowAttribute(handle, 38, &acrylic, sizeof(acrylic))) && acrylic == 3;
    const MARGINS margins = applied ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, 0, 0};
    const bool available = SUCCEEDED(DwmExtendFrameIntoClientArea(handle, &margins)) && applied;
    const int rounded = 2; DwmSetWindowAttribute(handle, 33, &rounded, sizeof(rounded));
    if (glassAvailable_ != available) { glassAvailable_ = available; emit appearanceChanged(); }
#endif
}
void MainWindow::showEvent(QShowEvent *event) {
#ifdef Q_OS_MACOS
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    if (QGuiApplication::platformName() == "cocoa") {
        auto *nativeWindow = ((__bridge NSView *)reinterpret_cast<void *>(winId())).window;
        nativeWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
        nativeWindow.titlebarAppearsTransparent = YES;
        nativeWindow.titleVisibility = NSWindowTitleHidden;
        if (@available(macOS 13.0, *)) {
            // Keep the main window separate from other applications' full-screen spaces.
            nativeWindow.collectionBehavior |= NSWindowCollectionBehaviorPrimary;
        }
    }
#endif
    QDialog::showEvent(event); updateBackdrop(); browse_.setActive(page_ == 0); refreshStartup();
}
} // namespace still
