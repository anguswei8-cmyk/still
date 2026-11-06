#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <optional>

namespace still {

class Hotkeys;
class HistoryStore;
class ClipboardArchive;
extern const QStringList hotkeyNames;
extern const QStringList hotkeyLabels;

class SettingsApply : public QObject {
    Q_OBJECT
public:
    SettingsApply(QJsonObject settings, QString settingsFile, Hotkeys &hotkeys, HistoryStore &history, QObject *parent = nullptr, ClipboardArchive *clipboard = nullptr);
    ~SettingsApply() override;
    const QJsonObject &settings() const { return settings_; }
    QStringList failedHotkeys() const { return failures_; }
    bool busy() const { return busy_ || pending_.has_value(); }
    void apply(QJsonObject changes);
signals:
    void finished(bool success, const QJsonObject &settings, const QStringList &failedHotkeys, const QString &message);
private:
    QJsonObject settings_;
    QString settingsFile_;
    Hotkeys &hotkeys_;
    HistoryStore &history_;
    ClipboardArchive *clipboard_;
    QStringList failures_;
    bool busy_ = false;
    std::optional<QJsonObject> pending_;
};

} // namespace still
