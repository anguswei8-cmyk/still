#include "main_window.h"
#include "clipboard/clipboard_archive.h"
#include "clipboard/clipboard_controller.h"
#include "settings/settings_apply.h"
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
#include <QResizeEvent>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSurfaceFormat>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace still {
namespace {
// Match Main.qml's sidebar and the two content margins when deciding whether preview fits.
constexpr int clipboardInsets = 176 + 2 * 24;
QVariantList fields(int page) {
    QVariantList rows;
    auto section = [&](const QString &title) { rows.append(QVariantMap{{"type", "section"}, {"title", title}}); };
    auto add = [&](const QString &type, const QString &group, const QString &key, const QString &title,
                   const QString &description, const QVariant &value, const QVariantMap &extra = {}) {
        QVariantMap row{{"type", type}, {"group", group}, {"key", key}, {"title", title}, {"description", description}, {"defaultValue", value}};
        for (auto it = extra.begin(); it != extra.end(); ++it) row.insert(it.key(), it.value());
        rows.append(row);
    };
    if (page == 1) {
        section("界面");
        add("choice", "appearance", "language", "界面语言", "", "zh_CN",
            {{"options", QVariantList{QVariantMap{{"text", "简体中文"}, {"value", "zh_CN"}}, QVariantMap{{"text", "English"}, {"value", "en"}}}}});
        add("choice", "appearance", "theme", "外观", "", "system",
            {{"options", QVariantList{QVariantMap{{"text", "跟随系统"}, {"value", "system"}}, QVariantMap{{"text", "浅色"}, {"value", "light"}}, QVariantMap{{"text", "深色"}, {"value", "dark"}}}}});
        section("截图");
        add("choice", "capture", "afterSelectAction", "选区完成后", "按 Enter 时执行的操作", "annotate",
            {{"options", QVariantList{QVariantMap{{"text", "打开编辑器"}, {"value", "annotate"}}, QVariantMap{{"text", "复制图片"}, {"value", "copy"}}, QVariantMap{{"text", "贴图到屏幕"}, {"value", "pin"}}, QVariantMap{{"text", "保存图片"}, {"value", "save"}}}}});
        add("switch", "capture", "showMagnifier", "显示放大镜", "精确查看边缘和像素颜色", true);
        add("switch", "capture", "snapToWindows", "自动吸附窗口", "单击即可选中鼠标下的窗口", true);
        section("贴图");
        add("switch", "pin", "wheelZoom", "滚轮缩放", "以鼠标位置为中心调整贴图大小", true);
        add("switch", "pin", "hideOnClickOutside", "点击外部时隐藏", "可通过快捷键或托盘菜单重新显示", false);
        section("文件保存");
        add("directory", "save", "dir", "默认保存目录", "", "", {{"placeholder", "系统图片文件夹"}});
        add("switch", "save", "copyAfterSave", "保存后复制图片", "保存到文件后，也放入剪贴板", false);
    } else if (page == 2) {
        section("全局快捷键");
        for (int i = 0; i < hotkeyNames.size(); ++i) add("shortcut", "hotkeys", hotkeyNames[i], hotkeyLabels[i], "", "");
    } else if (page == 3) {
        section("保存与空间");
        add("switch", "history", "enabled", "自动保留截图历史", "关闭后仍可复制、贴图和手动保存", true);
        add("directory", "history", "dir", "历史保存目录", "选择专用空文件夹，确认目录后自动迁移旧历史", "", {{"placeholder", "应用数据目录 / History"}});
        add("number", "history", "maxItems", "保留历史数量", "超出数量或容量后清理最早的截图", 200, {{"minimum", 1}, {"maximum", 5000}});
        add("number", "history", "maxMiB", "历史容量上限", "默认 1024 MiB，约 1 GiB", 1024, {{"minimum", 1}, {"maximum", 102400}, {"unit", "MiB"}});
    } else if (page == 4) {
        section("文字识别");
        add("text", "ocr", "language", "识别语言", "如 zh-Hans-CN 或 en-US，需系统支持对应语言", "zh-Hans-CN");
        section("贴图与历史");
        add("switch", "pin", "alwaysOnTop", "新贴图保持置顶", "", true);
        add("number", "pin", "maxPins", "最多贴图数", "超出后关闭最早创建的贴图", 60, {{"minimum", 1}, {"maximum", 200}});
        section("滚动长截图");
        add("number", "scroll", "intervalMs", "采样间隔", "请缓慢滚动，并避开固定表头和动画", 420, {{"minimum", 200}, {"maximum", 5000}, {"unit", "ms"}});
    } else if (page == 5) {
        section("记录与使用");
        add("switch", "clipboard", "enabled", "记录剪贴板历史", "文字、图片、文件路径与原始格式仅保存在本机", true);
        add("switch", "clipboard", "pasteOnSelect", "选择后直接粘贴", "回到之前的窗口；Ctrl+Enter 始终仅复制", true);
        section("本地保存");
        add("directory", "clipboard", "dir", "剪贴板保存目录", "独立于截图历史；改到其他磁盘后会迁移原有内容", "", {{"placeholder", "应用数据目录 / Clipboard"}});
        add("number", "clipboard", "maxItems", "保留条目数量", "优先清理最早的非固定条目", 200, {{"minimum", 1}, {"maximum", 2000}});
        add("number", "clipboard", "maxMiB", "剪贴板容量上限", "固定条目不自动删除，容量不足时停止新增", 256, {{"minimum", 1}, {"maximum", 10240}, {"unit", "MiB"}});
        add("folder", "clipboard", "", "查看本地数据", "文件条目只记录路径，不备份文件本体；历史重启后保留", "");
    }
    for (auto &value : rows) {
        auto row = value.toMap();
        for (const auto &key : {"title", "description", "placeholder"}) if (row.contains(key)) row[key] = t(row[key].toString());
        auto options = row.value("options").toList();
        for (auto &option : options) { auto item = option.toMap(); item["text"] = t(item["text"].toString()); option = item; }
        if (!options.isEmpty()) row["options"] = options;
        value = row;
    }
    return rows;
}
QVariantMap field(const QString &group, const QString &key) {
    for (int page = 1; page <= 5; ++page) for (const auto &value : fields(page)) {
        const auto row = value.toMap();
        if (!key.isEmpty() && row.value("group") == group && row.value("key") == key) return row;
    }
    return {};
}
std::optional<QJsonValue> settingValue(const QVariantMap &row, const QVariant &value) {
    const QString type = row.value("type").toString();
    if (type == "number") {
        bool ok = false; const double number = value.toDouble(&ok);
        if (!ok || value.metaType().id() == QMetaType::Bool || !std::isfinite(number) || number != std::floor(number) || number < row.value("minimum").toInt() || number > row.value("maximum").toInt()) return std::nullopt;
        return QJsonValue(int(number));
    }
    if (type == "switch") {
        if (value.metaType().id() != QMetaType::Bool) return std::nullopt;
        return QJsonValue(value.toBool());
    }
    if (value.metaType().id() != QMetaType::QString) return std::nullopt;
    const QString text = value.toString().trimmed();
    if (type == "choice") {
        bool found = false; for (const auto &option : row.value("options").toList()) if (option.toMap().value("value") == text) found = true;
        if (!found) return std::nullopt;
    }
    return QJsonValue(text);
}
} // namespace

QJsonObject normalizeSettings(QJsonObject form) {
    for (int page = 1; page <= 5; ++page) for (const auto &value : fields(page)) {
        const auto row = value.toMap(); const QString key = row.value("key").toString();
        if (key.isEmpty()) continue;
        const QString group = row.value("group").toString(); auto object = form.value(group).toObject();
        const auto valid = settingValue(row, object.value(key).toVariant());
        object[key] = valid.value_or(QJsonValue::fromVariant(row.value("defaultValue")));
        form[group] = object;
    }
    return form;
}
namespace {
QString kindName(const QString &kind) {
    if (kind == "image") return t("图片");
    if (kind == "rich") return t("富文本");
    if (kind == "files") return t("文件");
    if (kind == "link") return t("链接");
    return kind == "text" ? t("文字") : t("其他格式");
}
} // namespace

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
    : settings_(settings), history_(history), archive_(archive), clipboard_(clipboard), form_(normalizeSettings(settings.settings())),
      accepted_(form_) {
    setObjectName("SettingsShell"); setWindowTitle(t("拾影"));
    setAttribute(Qt::WA_DeleteOnClose); setMinimumSize(800, 620); resize(1080, 740);
#ifdef Q_OS_WIN
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    if (QGuiApplication::platformName() == "windows") setAttribute(Qt::WA_TranslucentBackground);
#endif
    images_ = new UiImages;
    QHash<QString, QImage> icons;
    const QList<QPair<QString, ToolIcon>> glyphs{{"clipboard", ToolIcon::Clipboard}, {"general", ToolIcon::Settings}, {"hotkeys", ToolIcon::Keyboard},
        {"history", ToolIcon::Image}, {"advanced", ToolIcon::Ocr}, {"settings", ToolIcon::Settings}, {"capture", ToolIcon::Capture},
        {"copy", ToolIcon::Copy}, {"star", ToolIcon::Star}, {"more", ToolIcon::Ellipsis}, {"pause", ToolIcon::Pause}, {"close", ToolIcon::Close}, {"edit", ToolIcon::Edit}, {"pin", ToolIcon::Pin},
        {"search", ToolIcon::Search}, {"trash", ToolIcon::Trash}, {"chevron", ToolIcon::ChevronDown},
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
        updateBackdrop();
        view_->engine()->retranslate();
        setWindowTitle(t("拾影"));
        status_ = InterfacePreferences::instance().translatedCaption(status_);
        emit appearanceChanged(); emit pageChanged(); emit statusChanged();
        refreshClips();
        if (page_ == 3) refreshHistory();
    });
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->addWidget(view_);
    clipboard_.attachHost(this);
    previewTimer_.setSingleShot(true); previewTimer_.setInterval(120);
    connect(&previewTimer_, &QTimer::timeout, this, [this] {
        if (isVisible() && page_ == 0 && previewVisible_ && width() - clipboardInsets >= 660 && !selectedId_.isEmpty())
            archive_.load(selectedId_, ClipboardArchive::ReadPurpose::Preview);
    });
    connect(&clipboard_, &ClipboardController::stateChanged, this, &MainWindow::clipboardStateChanged);
    connect(&clipboard_, &ClipboardController::feedback, this, &MainWindow::showMessage);
    connect(&archive_, &ClipboardArchive::changed, this, &MainWindow::refreshClips);
    connect(&archive_, &ClipboardArchive::ready, this, [this](const QString &id, const ClipPayload &payload, const QString &error, ClipboardArchive::ReadPurpose purpose) {
        if (purpose != ClipboardArchive::ReadPurpose::Preview || id != selectedId_ || page_ != 0 ||
            !isVisible() || !previewVisible_ || width() - clipboardInsets < 660) return;
        if (!error.isEmpty()) showMessage(error);
        previewText_ = payload.text;
        if (!payload.urls.isEmpty()) { QStringList paths; for (const auto &url : payload.urls) paths.append(url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toDisplayString()); previewText_ = paths.join('\n'); }
        previewText_ = previewText_.left(12000);
        images_->replace("preview/", {{"preview/current", payload.image}});
        previewImage_ = payload.image.isNull() ? QString() : QStringLiteral("image://content/preview/current?%1").arg(++previewRevision_);
        emit previewChanged();
    });
    connect(&history_, &HistoryStore::changed, this, [this] { if (page_ == 3) refreshHistory(); });
    connect(&settings_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &saved, const QStringList &failures, const QString &message) {
        if (settings_.busy()) { emit statusChanged(); return; }
        const auto effective = normalizeSettings(saved);
        // Preserve unfinished input and newer edits while an older save completes or rolls back.
        for (auto group = effective.begin(); group != effective.end(); ++group) {
            auto visible = form_.value(group.key()).toObject();
            if (!group.value().isObject()) { form_[group.key()] = group.value(); continue; }
            const auto requested = submitted_.value(group.key()).toObject();
            const auto previous = accepted_.value(group.key()).toObject();
            const auto fields = group.value().toObject();
            for (auto field = fields.begin(); field != fields.end(); ++field) {
                const auto expected = requested.contains(field.key()) ? requested.value(field.key()) : previous.value(field.key());
                if (visible.value(field.key()) == expected) visible[field.key()] = field.value();
            }
            form_[group.key()] = visible;
        }
        accepted_ = effective;
        submitted_ = {};
        statusError_ = !success || !failures.isEmpty() || !message.isEmpty();
        status_ = !success ? t("更改未生效，已恢复原设置：") + message
                          : failures.isEmpty() ? message : t("快捷键冲突，已保留原值：") + failures.join(QStringLiteral("、"));
        if (success && !failures.isEmpty() && !message.isEmpty()) status_ += QStringLiteral("；") + message;
        emit formChanged(); emit statusChanged();
    });
    if (!settings_.failedHotkeys().isEmpty()) { statusError_ = true; status_ = t("快捷键未注册：") + settings_.failedHotkeys().join(QStringLiteral("、")); }
    refreshClips();
    view_->setSource(QUrl("qrc:/qt/qml/Still/Main.qml"));
    if (view_->status() == QQuickWidget::Error) for (const auto &error : view_->errors()) qCritical().noquote() << error.toString();
}

MainWindow::~MainWindow() {
    historyThumbnails_.cancel();
    previewTimer_.stop(); archive_.cancelLoads(ClipboardArchive::ReadPurpose::Preview); clipboard_.attachHost(nullptr);
    delete view_; view_ = nullptr; images_ = nullptr;
}
QVariantList MainWindow::settingRows() const { return fields(page_); }
bool MainWindow::busy() const { return settings_.busy(); }
bool MainWindow::dark() const { return InterfacePreferences::instance().dark(); }
bool MainWindow::moveWindow() { return windowHandle() && windowHandle()->startSystemMove(); }
bool MainWindow::resizeWindow(int edges) { return !isMaximized() && windowHandle() && windowHandle()->startSystemResize(Qt::Edges(edges)); }
void MainWindow::toggleMaximized() { isMaximized() ? showNormal() : showMaximized(); }
void MainWindow::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) emit windowStateChanged();
}
int MainWindow::totalCount() const { return archive_.entries().size(); }
bool MainWindow::paused() const { return clipboard_.paused(); }
bool MainWindow::pasteDefault() const { return clipboard_.pasteDefault(); }
void MainWindow::setPage(int page) {
    if (page < 0 || page > 5 || page == page_) return;
    commitDrafts(); clipboard_.cancel();
    if (page_ == 3) { ++historyGeneration_; ++historyOpenGeneration_; historyThumbnails_.cancel(); }
    page_ = page; showMessage({}); emit pageChanged(); updatePreview();
    if (page == 3) refreshHistory();
}
void MainWindow::focusSearch() { setPage(0); view_->setFocus(); updatePreview(); emit focusSearchRequested(); }
void MainWindow::showMessage(const QString &message) { message_ = message; emit messageChanged(); }

void MainWindow::setDraft(const QString &group, const QString &key, const QVariant &value) {
    if (field(group, key).isEmpty()) return;
    auto object = form_.value(group).toObject(); object[key] = QJsonValue::fromVariant(value); form_[group] = object;
    emit formChanged();
}
void MainWindow::setSetting(const QString &group, const QString &key, const QVariant &value) {
    const auto row = field(group, key); if (row.isEmpty()) return;
    const auto valid = settingValue(row, value); if (!valid) return;
    auto visible = form_.value(group).toObject(); visible[key] = *valid; form_[group] = visible;
    emit formChanged();
    auto requested = submitted_.value(group).toObject();
    if (*valid == (requested.contains(key) ? requested.value(key) : accepted_.value(group).toObject().value(key))) return;
    requested[key] = *valid; submitted_[group] = requested;
    status_ = t("正在保存…"); statusError_ = false;
    settings_.apply({{group, QJsonObject{{key, *valid}}}});
    emit statusChanged();
}
void MainWindow::commitDrafts() {
    const auto draft = form_;
    for (int page = 1; page <= 5; ++page) for (const auto &value : fields(page)) {
        const auto row = value.toMap(); const QString group = row.value("group").toString(), key = row.value("key").toString();
        const auto requested = submitted_.value(group).toObject();
        const auto expected = requested.contains(key) ? requested.value(key) : accepted_.value(group).toObject().value(key);
        const auto draftValue = draft.value(group).toObject().value(key);
        if (!key.isEmpty() && draftValue != expected) setSetting(group, key, draftValue.toVariant());
    }
}
void MainWindow::setShortcut(const QString &name, int key, int modifiers) {
    if (!hotkeyNames.contains(name) || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_unknown) return;
    const QString sequence = key == Qt::Key_Backspace || key == Qt::Key_Delete ? QString() : QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(QKeySequence::PortableText);
    setSetting("hotkeys", name, sequence);
}
QString MainWindow::shortcutText(const QString &name) const { return QKeySequence::fromString(form_.value("hotkeys").toObject().value(name).toString(), QKeySequence::PortableText).toString(QKeySequence::NativeText); }
void MainWindow::browseDirectory(const QString &group) {
    if (group != "save" && group != "history" && group != "clipboard") return;
    const QString path = QFileDialog::getExistingDirectory(this, group == "save" ? t("选择保存目录") : t("选择专用空文件夹，迁移已有历史"), form_.value(group).toObject().value("dir").toString());
    if (!path.isEmpty()) setSetting(group, "dir", path);
}
void MainWindow::openDirectory(const QString &group) {
    const QString path = group == "history" ? history_.directory() : group == "clipboard" ? archive_.directory() : QString();
    if (!path.isEmpty()) { QDir().mkpath(path); QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
}

void MainWindow::setSearch(const QString &search) { if (search_ == search) return; search_ = search; emit searchChanged(); refreshClips(); }
void MainWindow::setFilter(const QString &filter) {
    if (filter_ == filter || !QStringList{"all", "text", "image", "files", "other", "pinned"}.contains(filter)) return;
    filter_ = filter; emit filterChanged(); refreshClips();
}
QVariantMap MainWindow::selected() const { return selectedIndex_ >= 0 && selectedIndex_ < clips_.size() ? clips_[selectedIndex_].toMap() : QVariantMap(); }
void MainWindow::refreshClips() {
    const QString previous = selectedId_; const int previousIndex = selectedIndex_;
    clips_.clear(); filtered_.clear(); QHash<QString, QImage> thumbnails;
    for (const auto &entry : archive_.entries()) {
        if (filter_ == "pinned" && !entry.pinned) continue;
        if (filter_ == "text" && entry.kind != "text" && entry.kind != "rich") continue;
        if ((filter_ == "image" || filter_ == "files") && entry.kind != filter_) continue;
        if (filter_ == "other" && entry.kind != "link" && entry.kind != "data") continue;
        if (!search_.trimmed().isEmpty() && !(entry.title + '\n' + entry.preview + '\n' + entry.formats.join(' ')).contains(search_.trimmed(), Qt::CaseInsensitive)) continue;
        const QString imageKey = "clip/" + entry.id;
        if (!entry.thumbnail.isNull()) thumbnails.insert(imageKey, entry.thumbnail);
        QString title = entry.title;
        // Generated metadata changes language; copied text and file names never do.
        if (entry.kind == "image" && title.contains(QStringLiteral(" · "))) title = kindName("image") + QStringLiteral(" · ") + title.section(QStringLiteral(" · "), 1);
        if (entry.kind == "files") {
            const auto count = QRegularExpression(QStringLiteral("^(\\d+) (?:个文件|files)$")).match(title);
            if (count.hasMatch()) title = t("%1 个文件").arg(count.captured(1));
        }
        if (entry.kind == "data" && (title == QStringLiteral("其他格式内容") || title == "Other format content")) title = t("其他格式内容");
        clips_.append(QVariantMap{{"id", entry.id}, {"kind", entry.kind}, {"title", title}, {"preview", entry.preview}, {"pinned", entry.pinned},
            {"kindLabel", kindName(entry.kind)}, {"createdText", entry.created.toString("MM-dd HH:mm")}, {"createdMs", entry.created.toMSecsSinceEpoch()},
            {"detail", kindName(entry.kind) + QStringLiteral("  %1  %2").arg(entry.created.toString("MM-dd HH:mm"), entry.pinned ? t("已固定") : QString())},
            {"formats", entry.formats.join('\n')}, {"thumbnail", entry.thumbnail.isNull() ? QString() : "image://content/" + imageKey}});
        filtered_.append(entry);
    }
    images_->replace("clip/", thumbnails);
    int index = -1; for (int i = 0; i < filtered_.size(); ++i) if (filtered_[i].id == previous) index = i;
    if (index < 0 && !filtered_.isEmpty()) index = qBound(0, previousIndex, int(filtered_.size()) - 1);
    selectedIndex_ = index; selectedId_ = index < 0 ? QString() : filtered_[index].id;
    if (selectedId_ != previous) showMessage({});
    emit clipsChanged(); emit selectionChanged(); updatePreview();
}
void MainWindow::setSelectedIndex(int index) {
    index = clips_.isEmpty() ? -1 : qBound(0, index, int(clips_.size()) - 1);
    if (index == selectedIndex_) return;
    selectedIndex_ = index; selectedId_ = index < 0 ? QString() : filtered_[index].id;
    clipboard_.cancel(); showMessage({}); emit selectionChanged(); updatePreview();
}
void MainWindow::setPreviewVisible(bool visible) { if (previewVisible_ == visible) return; previewVisible_ = visible; updatePreview(); }
void MainWindow::updatePreview() {
    archive_.cancelLoads(ClipboardArchive::ReadPurpose::Preview);
    previewTimer_.stop(); images_->replace("preview/", {}); previewImage_.clear();
    previewText_ = selected().value("preview").toString(); emit previewChanged();
    if (page_ == 0 && previewVisible_ && width() - clipboardInsets >= 660 && !selectedId_.isEmpty()) previewTimer_.start();
}
void MainWindow::activate(const QString &action) {
    if (selectedId_.isEmpty()) return;
    ClipAction requested = clipboard_.pasteDefault() ? ClipAction::Paste : ClipAction::Copy;
    if (action == "copy") requested = ClipAction::Copy;
    else if (action == "plain") requested = ClipAction::PlainPaste;
    else if (action == "edit") requested = ClipAction::EditImage;
    else if (action == "pin") requested = ClipAction::PinImage;
    else if (action == "save") requested = ClipAction::SaveImage;
    else if (!action.isEmpty()) return;
    const QString kind = selected().value("kind").toString();
    if ((action == "edit" || action == "pin" || action == "save") && kind != "image") return;
    clipboard_.activate(selectedId_, requested);
}
void MainWindow::togglePin() { if (!selectedId_.isEmpty()) archive_.pin(selectedId_, !selected().value("pinned").toBool()); }
void MainWindow::removeSelected() { if (!selectedId_.isEmpty()) archive_.remove(selectedId_); }
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
    previewTimer_.stop(); images_->replace("preview/", {}); previewImage_.clear();
    archive_.cancelLoads(ClipboardArchive::ReadPurpose::Preview);
    QDialog::hideEvent(event);
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
void MainWindow::showEvent(QShowEvent *event) { QDialog::showEvent(event); updateBackdrop(); updatePreview(); }
void MainWindow::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (images_ && (event->oldSize().width() - clipboardInsets >= 660) != (width() - clipboardInsets >= 660)) updatePreview();
}
} // namespace still
