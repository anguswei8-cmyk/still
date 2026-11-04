#include "ui/i18n.h"
#include "clipboard_controller.h"
#include "platform/platform.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QTimer>
#include <QUuid>
#include <QWidget>

namespace pixclone {
namespace { constexpr auto marker = "application/x-pixclone-clipboard-id"; }

ClipboardController::ClipboardController(ClipboardArchive &archive, QJsonObject options, bool monitor, QObject *parent)
    : QObject(parent), archive_(archive) {
    configure(options);
    if (monitor) connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ClipboardController::capture);
    connect(&archive_, &ClipboardArchive::errorOccurred, this, [this](const QString &text) { emit feedback(text); emit message(text); });
    connect(&archive_, &ClipboardArchive::ready, this, [this](const QString &id, const ClipPayload &payload, const QString &error, ClipboardArchive::ReadPurpose purpose) {
        if (purpose != ClipboardArchive::ReadPurpose::Action || id != pendingId_ || !host_) return;
        const ClipAction action = pendingAction_;
        pendingId_.clear();
        if (!error.isEmpty() && (action != ClipAction::PlainPaste || payload.empty())) { emit feedback(error); return; }
        if (action == ClipAction::EditImage || action == ClipAction::PinImage || action == ClipAction::SaveImage) {
            if (payload.image.isNull()) { emit feedback(t("该条目不是可用图片")); return; }
            if (action == ClipAction::EditImage) emit editImage(payload.image);
            else if (action == ClipAction::PinImage) emit pinImage(payload.image);
            else emit saveImage(payload.image);
            return;
        }
        QMimeData *mime = nullptr;
        if (action == ClipAction::PlainPaste) {
            QString text = payload.text;
            if (!payload.urls.isEmpty()) { QStringList urls; for (const auto &url : payload.urls) urls.append(url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toDisplayString()); text = urls.join('\n'); }
            if (text.isEmpty()) { emit feedback(t("该条目没有可粘贴的文字")); return; }
            mime = new QMimeData; mime->setText(text);
        } else mime = restoreClipboard(payload);
        restoreToken_ = QUuid::createUuid().toByteArray(); restoreTime_.start(); mime->setData(marker, restoreToken_);
        writing_ = true; QGuiApplication::clipboard()->setMimeData(mime); writing_ = false;
        const auto *written = QGuiApplication::clipboard()->mimeData();
        if (!written || written->data(marker) != restoreToken_) { emit feedback(t("系统剪贴板正忙，内容未复制，请重试")); return; }
        if (action == ClipAction::Copy || !target_) { emit feedback(t("已复制剪贴板内容，可切回目标窗口粘贴")); return; }
        const auto target = target_;
        emit dismissRequested();
        const quint64 generation = generation_;
        const auto token = restoreToken_;
        if (!activatePasteTarget(target)) { emit message(t("内容已复制，请切回目标窗口手动粘贴")); return; }
        QTimer::singleShot(150, this, [this, generation, token, target] {
            const auto *current = QGuiApplication::clipboard()->mimeData();
            if (generation != generation_ || !current || current->data(marker) != token) return;
            QString error;
            if (!pasteToTarget(target, &error)) emit message(error);
        });
    });
}

void ClipboardController::attachHost(QWidget *host) {
    if (host_) { host_->removeEventFilter(this); disconnect(host_, nullptr, this, nullptr); }
    cancel(); host_ = host;
    if (host_) {
        host_->installEventFilter(this);
        connect(host_, &QObject::destroyed, this, &ClipboardController::cancel);
    }
    emit stateChanged();
}
void ClipboardController::setPaused(bool paused) {
    if (!enabled_) { emit settingsRequested(); return; }
    paused_ = paused;
    emit stateChanged();
    emit feedback(paused ? t("已暂停记录；历史仍可使用。") : t("已继续记录新的复制内容。"));
}
void ClipboardController::configure(const QJsonObject &options) {
    enabled_ = options.value("enabled").toBool(true);
    pasteDefault_ = options.value("pasteOnSelect").toBool(true);
    emit stateChanged();
}
void ClipboardController::cancel() {
    if (!pendingId_.isEmpty()) archive_.cancelLoads(ClipboardArchive::ReadPurpose::Action);
    pendingId_.clear();
    ++generation_;
}
void ClipboardController::prepare() {
    cancel();
    // Only an explicit external entry may replace the reliable paste target.
    if (!host_ || QApplication::activeWindow() != host_) target_ = currentPasteTarget();
    emit stateChanged();
}
bool ClipboardController::eventFilter(QObject *object, QEvent *event) {
    if (host_ && object == host_ && event->type() == QEvent::WindowDeactivate &&
        !QApplication::activePopupWidget() && !QApplication::activeModalWidget()) {
        target_ = 0;
        emit stateChanged();
        if (!pendingId_.isEmpty()) cancel();
    }
    if (object == host_ && event->type() == QEvent::Hide) cancel();
    return QObject::eventFilter(object, event);
}
void ClipboardController::activate(const QString &id, ClipAction action) {
    cancel(); pendingId_ = id; pendingAction_ = action; archive_.load(id, ClipboardArchive::ReadPurpose::Action);
}
void ClipboardController::capture() {
    if (!enabled_ || paused_ || writing_ || capturing_) return;
    const auto *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) return;
    if (restoreTime_.isValid() && restoreTime_.elapsed() < 500 && mime->data(marker) == restoreToken_) return;
    capturing_ = true;
    QString error;
    const auto payload = captureClipboard(*mime, &error);
    capturing_ = false;
    if (!error.isEmpty()) { emit feedback(error); emit message(error); }
    else if (!payload.empty()) archive_.add(payload);
}

} // namespace pixclone
