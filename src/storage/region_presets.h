#pragma once

#include <QRectF>
#include <QString>
#include <QVector>
#include <optional>

namespace still {

struct RegionScreen {
    QString name, manufacturer, model, serialNumber;
};

struct RegionPreset {
    QString name;
    RegionScreen screen;
    QRectF region;
};

std::optional<QVector<RegionPreset>> loadRegionPresets(const QString &path, QString *error);
bool saveRegionPresets(const QString &path, const QVector<RegionPreset> &presets, QString *error);
int matchingPresetScreen(const RegionScreen &saved, const QVector<RegionScreen> &screens);

} // namespace still
