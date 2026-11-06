#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QVariantList>
#include <optional>

namespace still {

QVariantList backupSettingRows();
QJsonObject defaultSettings();
QJsonObject settingsBackupDocument(const QJsonObject &current);
std::optional<QJsonObject> parseSettingsBackup(const QByteArray &bytes, QString *error);
bool saveSettingsBackup(const QString &path, const QJsonObject &current, QString *error);

} // namespace still
