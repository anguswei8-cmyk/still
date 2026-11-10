#include "settings_backup.h"
#include "settings_apply.h"
#include "settings_draft.h"
#include "storage/image_export.h"
#include "storage/store.h"
#include "ui/i18n.h"

#include <QDir>
#include <QJsonDocument>
#include <QKeySequence>
#include <QSet>
#include <cmath>
#include <limits>

namespace still {
namespace {

bool absoluteBackupPath(const QString &path) {
    // Qt treats drive-relative Windows paths such as C:folder as absolute.
    if (path.isEmpty() || path.startsWith('/')) return true;
    if (path.size() >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) return true;
    return path.startsWith("\\\\") && path.mid(2).split('\\', Qt::SkipEmptyParts).size() >= 2;
}

bool validValue(const QVariantMap &row, const QJsonValue &value) {
    const auto type = row.value("type").toString();
    if (type == "switch") return value.isBool();
    if (type == "number" || type == "decimal") {
        const double number = value.toDouble();
        return value.isDouble() && std::isfinite(number) && (type == "decimal" || number == std::floor(number))
            && number >= row.value("minimum").toDouble() && number <= row.value("maximum").toDouble();
    }
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.contains(QChar::Null) || text != text.trimmed()) return false;
    if (type == "directory") return absoluteBackupPath(text);
    if (type == "template") return validImageNameTemplate(text);
    if (type == "choice") {
        for (const auto &option : row.value("options").toList()) if (option.toMap().value("value").toString() == text) return true;
        return false;
    }
    if (type == "shortcut" && !text.isEmpty()) {
        const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
        if (sequence.count() != 1) return false;
        const auto key = sequence[0].key();
        return key != Qt::Key_unknown && key != Qt::Key(0) && key != Qt::Key_Control && key != Qt::Key_Shift
            && key != Qt::Key_Alt && key != Qt::Key_Meta && key != Qt::Key_AltGr;
    }
    return true;
}

bool validSettings(const QJsonObject &settings, QString *error) {
    const auto rows = backupSettingRows();
    QHash<QString, QSet<QString>> fields;
    for (const auto &value : rows) {
        const auto row = value.toMap(); fields[row.value("group").toString()].insert(row.value("key").toString());
    }
    if (settings.size() != fields.size()) { if (error) *error = t("配置备份格式无效或版本不支持。"); return false; }
    for (auto group = fields.cbegin(); group != fields.cend(); ++group) {
        const auto value = settings.value(group.key());
        const auto object = value.toObject();
        if (!value.isObject() || object.size() != group->size()) { if (error) *error = t("配置备份格式无效或版本不支持。"); return false; }
        for (auto field = object.begin(); field != object.end(); ++field) {
            if (!group->contains(field.key())) { if (error) *error = t("配置备份格式无效或版本不支持。"); return false; }
        }
    }
    QList<QKeySequence> shortcuts;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto setting = settings.value(row.value("group").toString()).toObject().value(row.value("key").toString());
        if (!validValue(row, setting)) { if (error) *error = t("配置备份中的设置无效：%1").arg(row.value("title").toString()); return false; }
        if (row.value("type") == "shortcut" && !setting.toString().isEmpty()) {
            const auto sequence = QKeySequence::fromString(setting.toString(), QKeySequence::PortableText);
            if (shortcuts.contains(sequence)) { if (error) *error = t("配置备份中的快捷键重复：%1").arg(row.value("title").toString()); return false; }
            shortcuts.append(sequence);
        }
    }
    return true;
}

} // namespace

QVariantList backupSettingRows() {
    QVariantList rows;
    for (int page = 1; page <= 5; ++page) for (const auto &value : SettingsDraft::rows(page)) {
        auto row = value.toMap();
        if (row.value("key").toString().isEmpty()) continue;
        rows.append(row);
    }
    auto add = [&](const QString &type, const QString &group, const QString &key, const QString &title, const QVariant &value, double minimum = 0, double maximum = 0) {
        rows.append(QVariantMap{{"type", type}, {"group", group}, {"key", key}, {"title", title}, {"defaultValue", value}, {"minimum", minimum}, {"maximum", maximum}});
    };
    add("number", "capture", "maxEdge", t("截图最大边长"), 0, 0, std::numeric_limits<int>::max());
    add("switch", "pin", "confirmDoubleClickClose", t("双击关闭贴图前确认"), true);
    add("decimal", "pin", "opacity", t("新贴图不透明度"), 1.0, 0.15, 1.0);
    add("number", "scroll", "maxHeightPx", t("长截图最大高度"), 24000, 1000, 50000);
    add("number", "scroll", "maxFrames", t("长截图最大帧数"), 80, 2, 300);
    return rows;
}

QJsonObject defaultSettings() {
    auto settings = normalizeSettings({}); settings["hotkeys"] = defaultHotkeys();
    for (const auto &value : backupSettingRows()) {
        const auto row = value.toMap(); const auto group = row.value("group").toString(), key = row.value("key").toString();
        auto object = settings.value(group).toObject();
        if (!object.contains(key)) object[key] = QJsonValue::fromVariant(row.value("defaultValue"));
        settings[group] = object;
    }
    return settings;
}

QJsonObject settingsBackupDocument(const QJsonObject &current) {
    auto settings = defaultSettings();
    const auto rows = backupSettingRows();
    for (const auto &value : rows) {
        const auto row = value.toMap(); const auto group = row.value("group").toString(), key = row.value("key").toString();
        const auto saved = current.value(group).toObject().value(key);
        if (!saved.isUndefined()) { auto object = settings.value(group).toObject(); object[key] = saved; settings[group] = object; }
    }
    settings = normalizeSettings(settings);
    for (const auto &value : rows) {
        const auto row = value.toMap(); const auto group = row.value("group").toString(), key = row.value("key").toString();
        auto object = settings.value(group).toObject();
        const auto type = row.value("type").toString();
        if (type == "directory") {
            const auto path = object.value(key).toString();
            if (!absoluteBackupPath(path)) object[key] = QDir(path).absolutePath();
        } else if (key == "maxEdge" || key == "maxHeightPx" || key == "maxFrames") {
            object[key] = qBound(row.value("minimum").toInt(), object.value(key).toInt(row.value("defaultValue").toInt()), row.value("maximum").toInt());
        } else if (type == "decimal") {
            object[key] = qBound(row.value("minimum").toDouble(), object.value(key).toDouble(row.value("defaultValue").toDouble()), row.value("maximum").toDouble());
        } else if (key == "confirmDoubleClickClose" && !object.value(key).isBool()) object[key] = true;
        settings[group] = object;
    }
    return {{"format", "still-settings"}, {"version", 1}, {"settings", settings}};
}

std::optional<QJsonObject> parseSettingsBackup(const QByteArray &bytes, QString *error) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    const auto object = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || object.size() != 3
        || object.value("format") != QJsonValue("still-settings") || object.value("version") != QJsonValue(1)
        || !object.value("settings").isObject()) {
        if (error) *error = t("配置备份格式无效或版本不支持。"); return std::nullopt;
    }
    const auto settings = object.value("settings").toObject();
    if (!validSettings(settings, error)) return std::nullopt;
    return settings;
}

bool saveSettingsBackup(const QString &path, const QJsonObject &current, QString *error) {
    const auto document = settingsBackupDocument(current);
    if (!validSettings(document.value("settings").toObject(), error)) return false;
    return writeJson(path, document, error);
}

} // namespace still
