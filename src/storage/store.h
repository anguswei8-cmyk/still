#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>

namespace still {

QString dataDirectory();
bool isAbsoluteDirectoryPath(const QString &path);
QString uniqueImagePath(const QString &directory, const QString &fileName = {});
bool saveImage(const QImage &image, const QString &path, QString *error, qint64 maximumBytes = -1, int quality = 95, int compression = -1);
bool writeJson(const QString &path, const QJsonObject &object, QString *error = nullptr);
QJsonObject readJson(const QString &path);
QString historyDirectory(const QJsonObject &options = {});
QString addHistory(const QImage &image, int maximum, QString *error, const QString &directory = {}, qint64 maximumBytes = 1024LL * 1024 * 1024);
QStringList historyFiles(const QString &directory = {});
bool pruneHistory(const QString &directory, int maximum, qint64 maximumBytes, QString *error, const QString &newest = {});
QString settingsPath();

} // namespace still
