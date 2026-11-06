#pragma once

#include <QDateTime>
#include <QImage>
#include <QJsonObject>

namespace still {

inline const QString defaultImageNameTemplate = QStringLiteral("still_{date}_{time}_{ms}");
bool validImageNameTemplate(const QString &pattern);
QString imageExportFileName(const QJsonObject &options, const QDateTime &when = QDateTime::currentDateTime());
QString imageExportDirectory(const QJsonObject &options);
QString exportImage(const QImage &image, const QJsonObject &options, QString *error);

} // namespace still
