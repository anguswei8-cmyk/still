#include "ui/i18n.h"
#include "store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace pixclone {

QString dataDirectory() {
    const QString overridePath = qEnvironmentVariable("PIXCLONE_DATA_DIR");
    if (!overridePath.isEmpty()) return QDir(overridePath).absolutePath();
#ifdef Q_OS_WIN
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString roaming = qEnvironmentVariable("APPDATA");
    const QString localPath = QDir(local).filePath("PixClone");
    const QString roamingPath = QDir(roaming).filePath("PixClone");
    auto hasData = [](const QString &path) {
        return QFileInfo::exists(QDir(path).filePath("qt-settings.json"))
            || QFileInfo::exists(QDir(path).filePath("settings.json"))
            || QDir(QDir(path).filePath("History")).exists();
    };
    // Electron defaults to roaming AppData, despite the old handover claiming LocalAppData.
    if (!roaming.isEmpty() && hasData(roamingPath) && (local.isEmpty() || !hasData(localPath))) return roamingPath;
    if (!local.isEmpty()) return localPath;
#endif
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString settingsPath() { return QDir(dataDirectory()).filePath("qt-settings.json"); }

QString uniqueImagePath(const QString &directory) {
    const QString base = "pixclone_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString path = QDir(directory).filePath(base + ".png");
    for (int i = 2; QFileInfo::exists(path); ++i) path = QDir(directory).filePath(base + '-' + QString::number(i) + ".png");
    return path;
}

bool saveImage(const QImage &image, const QString &path, QString *error, qint64 maximumBytes) {
    if (image.isNull()) { if (error) *error = t("图片为空"); return false; }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QByteArray format = (suffix == "jpg" || suffix == "jpeg") ? QByteArray("jpeg") : QByteArray("png");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { if (error) *error = t("无法创建保存目录"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    QImageWriter writer(&file, format);
    writer.setQuality(95);
    if (!writer.write(image)) { if (error) *error = writer.errorString(); return false; }
    if (maximumBytes >= 0 && file.size() > maximumBytes) {
        if (error) *error = t("本次图片超过历史容量上限，未加入历史；可提高容量上限或手动保存");
        return false;
    }
    if (!file.commit()) { if (error) *error = file.errorString(); return false; }
    return true;
}

QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJson(const QString &path, const QJsonObject &object, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString historyDirectory(const QJsonObject &options) {
    const QString path = options.value("dir").toString().trimmed();
    return path.isEmpty() ? QDir(dataDirectory()).filePath("History") : QDir(path).absolutePath();
}

QStringList historyFiles(const QString &directory) {
    QDir dir(directory.isEmpty() ? historyDirectory() : directory);
    const auto files = dir.entryInfoList({"*.png"}, QDir::Files, QDir::Time);
    QStringList result;
    for (const auto &file : files) result.append(file.absoluteFilePath());
    return result;
}

bool pruneHistory(const QString &directory, int maximum, qint64 maximumBytes, QString *error, const QString &newest) {
    auto files = historyFiles(directory);
    // Multiple fast saves can share a filesystem timestamp; always preserve the just-written image.
    if (!newest.isEmpty()) { files.removeAll(newest); files.prepend(newest); }
    qint64 bytes = 0;
    for (const auto &path : files) bytes += QFileInfo(path).size();
    int retained = files.size();
    bool success = true;
    for (int i = files.size() - 1; i >= 0 && (retained > qMax(1, maximum) || bytes > maximumBytes); --i) {
        const qint64 size = QFileInfo(files[i]).size();
        if (QFile::remove(files[i])) { bytes -= size; --retained; }
        else {
            success = false;
            if (error) *error = t("部分旧历史无法清理，请检查目录权限或文件占用");
        }
    }
    return success;
}

QString addHistory(const QImage &image, int maximum, QString *error, const QString &directory, qint64 maximumBytes) {
    const QString dir = QDir(directory.isEmpty() ? historyDirectory() : directory).absolutePath();
    const QString path = QDir(dir).filePath(QString::number(QDateTime::currentMSecsSinceEpoch()) + '-' + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".png");
    if (!saveImage(image, path, error, maximumBytes)) return {};
    pruneHistory(dir, maximum, maximumBytes, error, path);
    return path;
}

} // namespace pixclone
