#include "region_presets.h"
#include "store.h"
#include "ui/i18n.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>

namespace still {
namespace {
bool validPreset(const RegionPreset &preset) {
    const auto &region = preset.region;
    return !preset.name.isEmpty() && preset.name == preset.name.trimmed() && preset.name.size() <= 80
        && std::isfinite(region.x()) && std::isfinite(region.y())
        && std::isfinite(region.width()) && std::isfinite(region.height())
        && std::isfinite(region.right()) && std::isfinite(region.bottom())
        && region.x() >= 0 && region.y() >= 0 && region.width() > 0 && region.height() > 0;
}
}

std::optional<QVector<RegionPreset>> loadRegionPresets(const QString &path, QString *error) {
    QFile file(path);
    if (!file.exists()) return QVector<RegionPreset>{};
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return std::nullopt; }
    const auto invalid = [&] {
        if (error) *error = t("区域预设文件损坏或版本不支持，原文件已保留。");
        return std::optional<QVector<RegionPreset>>{};
    };
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (file.error() != QFileDevice::NoError) { if (error) *error = file.errorString(); return std::nullopt; }
    const auto object = document.object();
    if (object.value("version") != 1 || !object.value("presets").isArray()) return invalid();
    QVector<RegionPreset> presets;
    QSet<QString> names;
    for (const auto &value : object.value("presets").toArray()) {
        const auto record = value.toObject(), screen = record.value("screen").toObject();
        const auto region = record.value("region").toArray();
        if (!record.value("name").isString() || !screen.value("name").isString()
            || !screen.value("manufacturer").isString() || !screen.value("model").isString()
            || !screen.value("serialNumber").isString() || region.size() != 4) return invalid();
        for (const auto &coordinate : region) if (!coordinate.isDouble()) return invalid();
        RegionPreset preset{record.value("name").toString(),
            {screen.value("name").toString(), screen.value("manufacturer").toString(),
             screen.value("model").toString(), screen.value("serialNumber").toString()},
            QRectF(region[0].toDouble(), region[1].toDouble(), region[2].toDouble(), region[3].toDouble())};
        if (!validPreset(preset) || names.contains(preset.name)) return invalid();
        names.insert(preset.name);
        presets.append(std::move(preset));
    }
    return presets;
}

bool saveRegionPresets(const QString &path, const QVector<RegionPreset> &presets, QString *error) {
    if (!loadRegionPresets(path, error)) return false;
    QJsonArray records;
    QSet<QString> names;
    for (const auto &preset : presets) {
        if (!validPreset(preset) || names.contains(preset.name)) {
            if (error) *error = t("区域预设名称或区域无效，未保存");
            return false;
        }
        names.insert(preset.name);
        const auto &screen = preset.screen;
        const auto &region = preset.region;
        records.append(QJsonObject{{"name", preset.name},
            {"screen", QJsonObject{{"name", screen.name}, {"manufacturer", screen.manufacturer},
                                   {"model", screen.model}, {"serialNumber", screen.serialNumber}}},
            {"region", QJsonArray{region.x(), region.y(), region.width(), region.height()}}});
    }
    return writeJson(path, {{"version", 1}, {"presets", records}}, error);
}

int matchingPresetScreen(const RegionScreen &saved, const QVector<RegionScreen> &screens) {
    if (saved.serialNumber.isEmpty() && saved.name.isEmpty()) return -1;
    int match = -1;
    for (int i = 0; i < screens.size(); ++i) {
        const auto &screen = screens[i];
        const bool same = saved.manufacturer == screen.manufacturer && saved.model == screen.model
            && (saved.serialNumber.isEmpty() ? saved.name == screen.name : saved.serialNumber == screen.serialNumber);
        if (same) {
            if (match != -1) return -1;
            match = i;
        }
    }
    return match;
}

} // namespace still
