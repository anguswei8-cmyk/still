#include "ui/i18n.h"
#include "settings_apply.h"
#include "storage/history.h"
#include "platform/platform.h"
#include "storage/store.h"
#include "clipboard/clipboard_archive.h"

namespace still {

namespace {
void mergeChanges(QJsonObject &settings, const QJsonObject &changes) {
    for (auto group = changes.begin(); group != changes.end(); ++group) {
        if (!group.value().isObject()) { settings[group.key()] = group.value(); continue; }
        auto merged = settings.value(group.key()).toObject();
        const auto fields = group.value().toObject();
        for (auto field = fields.begin(); field != fields.end(); ++field) merged[field.key()] = field.value();
        settings[group.key()] = merged;
    }
}
}

const QStringList hotkeyNames{"capture", "quickCapture", "fullScreen", "pasteAsPin", "togglePins", "scrollCapture", "closePins", "clipboardHistory"};
const QStringList hotkeyLabels{t("截图并编辑"), t("快速截图复制"), t("整屏截图并贴图"),
    t("剪贴板图片贴图"), t("显示/隐藏全部贴图"), t("滚动长截图"), t("关闭全部贴图"), t("呼出剪贴板历史")};

SettingsApply::SettingsApply(QJsonObject settings, QString settingsFile, Hotkeys &hotkeys, HistoryStore &history, QObject *parent, ClipboardArchive *clipboard)
    : QObject(parent), settings_(std::move(settings)), settingsFile_(std::move(settingsFile)), hotkeys_(hotkeys), history_(history), clipboard_(clipboard) {
    const auto bindings = settings_.value("hotkeys").toObject();
    for (int i = 0; i < hotkeyNames.size(); ++i)
        if (!hotkeys_.bind(i, QKeySequence::fromString(bindings.value(hotkeyNames[i]).toString(), QKeySequence::PortableText))) failures_.append(t(hotkeyLabels[i]));
}

SettingsApply::~SettingsApply() { hotkeys_.discardStaged(); }

void SettingsApply::apply(QJsonObject changes) {
    // Rebase queued changes on the committed result, including after a failed save.
    if (busy()) {
        if (!pending_) pending_.emplace();
        mergeChanges(*pending_, changes);
        return;
    }
    if (history_.isConfiguring()) { emit finished(false, settings_, failures_, t("历史目录正在切换，请稍后重试")); return; }
    busy_ = true;
    auto candidate = settings_;
    mergeChanges(candidate, changes);
    auto bindings = candidate.value("hotkeys").toObject();
    const auto previous = settings_.value("hotkeys").toObject();
    QStringList failures;
    for (int i = 0; i < hotkeyNames.size(); ++i) {
        const auto sequence = QKeySequence::fromString(bindings.value(hotkeyNames[i]).toString(), QKeySequence::PortableText);
        if (!hotkeys_.stage(i, sequence)) {
            failures.append(t(hotkeyLabels[i]));
            bindings[hotkeyNames[i]] = previous.value(hotkeyNames[i]);
        }
    }
    candidate["hotkeys"] = bindings;
    connect(&history_, &HistoryStore::reconfigured, this, [this, candidate, failures](bool success, const QString &message) {
        if (success) {
            hotkeys_.commitStaged();
            settings_ = candidate;
            failures_ = failures;
        } else hotkeys_.discardStaged();
        busy_ = false;
        emit finished(success, settings_, success ? failures : failures_, message);
        if (!busy_ && pending_) {
            auto next = std::move(*pending_);
            pending_.reset();
            apply(std::move(next));
        }
    }, Qt::SingleShotConnection);
    const QString file = settingsFile_;
    const auto clipOptions = candidate.value("clipboard").toObject();
    const auto previousClips = settings_.value("clipboard").toObject();
    const bool changeArchive = clipOptions.value("dir") != previousClips.value("dir") ||
        clipOptions.value("maxItems") != previousClips.value("maxItems") ||
        clipOptions.value("maxMiB") != previousClips.value("maxMiB");
    // This callback runs on the history worker; it captures values, never UI or hotkey objects.
    history_.reconfigure(candidate.value("history").toObject(), [file, candidate, archive = changeArchive ? clipboard_ : nullptr, clipOptions](QString &error) {
        auto commit = [file, candidate](QString &message) { return writeJson(file, candidate, &message); };
        return archive ? archive->reconfigureBlocking(clipOptions, commit, error) : commit(error);
    });
}

} // namespace still
