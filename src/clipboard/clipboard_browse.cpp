#include "clipboard_browse.h"
#include "ui/i18n.h"
#include <QDir>
#include <QRegularExpression>

namespace still {
namespace {
QString kindName(const QString &kind) {
    if (kind == "image") return t("图片");
    if (kind == "rich") return t("富文本");
    if (kind == "files") return t("文件");
    if (kind == "link") return t("链接");
    return kind == "text" ? t("文字") : t("其他格式");
}
} // namespace

ClipboardBrowse::ClipboardBrowse(ClipboardArchive &archive, QObject *parent)
    : QObject(parent), archive_(archive) {
    previewTimer_.setSingleShot(true); previewTimer_.setInterval(120);
    connect(&previewTimer_, &QTimer::timeout, this, [this] {
        if (canPreview())
            archive_.load(selectedId_, ClipboardArchive::ReadPurpose::Preview);
    });
    connect(&archive_, &ClipboardArchive::changed, this, &ClipboardBrowse::refreshClips);
    connect(&archive_, &ClipboardArchive::ready, this, [this](const QString &id, const ClipPayload &payload, const QString &error, ClipboardArchive::ReadPurpose purpose) {
        if (purpose != ClipboardArchive::ReadPurpose::Preview || id != selectedId_ || !canPreview()) return;
        if (!error.isEmpty()) emit message(error);
        previewText_ = payload.text;
        if (!payload.urls.isEmpty()) { QStringList paths; for (const auto &url : payload.urls) paths.append(url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toDisplayString()); previewText_ = paths.join('\n'); }
        previewText_ = previewText_.left(12000);
        previewImage_ = payload.image;
        previewUrl_ = payload.image.isNull() ? QString() : QStringLiteral("image://content/preview/current?%1").arg(++previewRevision_);
        emit previewChanged();
    });
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, &ClipboardBrowse::refreshClips);
    refreshClips();
}

ClipboardBrowse::~ClipboardBrowse() { archive_.cancelLoads(ClipboardArchive::ReadPurpose::Preview); }

bool ClipboardBrowse::canPreview() const { return active_ && previewFits_ && previewVisible_ && !selectedId_.isEmpty(); }
void ClipboardBrowse::setActive(bool active) {
    if (active_ == active) return;
    active_ = active; updatePreview();
}
void ClipboardBrowse::setPreviewFits(bool fits) {
    if (previewFits_ == fits) return;
    previewFits_ = fits; updatePreview();
}

void ClipboardBrowse::setSearch(const QString &search) { if (search_ == search) return; search_ = search; emit searchChanged(); refreshClips(); }
void ClipboardBrowse::setFilter(const QString &filter) {
    if (filter_ == filter || !QStringList{"all", "text", "image", "files", "other", "pinned"}.contains(filter)) return;
    filter_ = filter; emit filterChanged(); refreshClips();
}
QVariantMap ClipboardBrowse::selected() const { return selectedIndex_ >= 0 && selectedIndex_ < clips_.size() ? clips_[selectedIndex_].toMap() : QVariantMap(); }
void ClipboardBrowse::refreshClips() {
    const QString previous = selectedId_; const int previousIndex = selectedIndex_;
    clips_.clear(); thumbnails_.clear();
    for (const auto &entry : archive_.entries()) {
        if (filter_ == "pinned" && !entry.pinned) continue;
        if (filter_ == "text" && entry.kind != "text" && entry.kind != "rich") continue;
        if ((filter_ == "image" || filter_ == "files") && entry.kind != filter_) continue;
        if (filter_ == "other" && entry.kind != "link" && entry.kind != "data") continue;
        if (!search_.trimmed().isEmpty() && !(entry.title + '\n' + entry.preview + '\n' + entry.formats.join(' ')).contains(search_.trimmed(), Qt::CaseInsensitive)) continue;
        const QString imageKey = "clip/" + entry.id;
        if (!entry.thumbnail.isNull()) thumbnails_.insert(imageKey, entry.thumbnail);
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
    }
    int index = -1; for (int i = 0; i < clips_.size(); ++i) if (clips_[i].toMap().value("id").toString() == previous) index = i;
    if (index < 0 && !clips_.isEmpty()) index = qBound(0, previousIndex, int(clips_.size()) - 1);
    selectedIndex_ = index; selectedId_ = index < 0 ? QString() : clips_[index].toMap().value("id").toString();
    if (selectedId_ != previous) emit message({});
    emit clipsChanged(); emit selectionChanged(); updatePreview();
}
bool ClipboardBrowse::setSelectedIndex(int index) {
    index = clips_.isEmpty() ? -1 : qBound(0, index, int(clips_.size()) - 1);
    if (index == selectedIndex_) return false;
    selectedIndex_ = index; selectedId_ = index < 0 ? QString() : clips_[index].toMap().value("id").toString();
    emit message({}); emit selectionChanged(); updatePreview();
    return true;
}
void ClipboardBrowse::setPreviewVisible(bool visible) { if (previewVisible_ == visible) return; previewVisible_ = visible; updatePreview(); }
void ClipboardBrowse::updatePreview() {
    archive_.cancelLoads(ClipboardArchive::ReadPurpose::Preview);
    previewTimer_.stop(); previewImage_ = {}; previewUrl_.clear();
    previewText_ = selected().value("preview").toString(); emit previewChanged();
    if (canPreview()) previewTimer_.start();
}
} // namespace still
