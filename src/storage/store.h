#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>

namespace pixclone {

QString dataDirectory();
QString uniqueImagePath(const QString &directory);
bool saveImage(const QImage &image, const QString &path, QString *error, qint64 maximumBytes = -1);
bool writeJson(const QString &path, const QJsonObject &object, QString *error = nullptr);
QJsonObject readJson(const QString &path);
QString historyDirectory(const QJsonObject &options = {});
QString addHistory(const QImage &image, int maximum, QString *error, const QString &directory = {}, qint64 maximumBytes = 1024LL * 1024 * 1024);
QStringList historyFiles(const QString &directory = {});
bool pruneHistory(const QString &directory, int maximum, qint64 maximumBytes, QString *error, const QString &newest = {});
QString settingsPath();

} // namespace pixclone
