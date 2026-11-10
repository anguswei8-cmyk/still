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

namespace still {

bool isAbsoluteDirectoryPath(const QString &path) {
    if (path.contains(QChar::Null)) return false;
#ifdef Q_OS_WIN
    // Qt accepts C:folder and /folder, whose targets still depend on a current drive/directory.
    const QString normalized = QDir::fromNativeSeparators(path);
    if (normalized.startsWith("//")) return normalized.mid(2).split('/', Qt::SkipEmptyParts).size() >= 2;
    return normalized.size() >= 3 && ((normalized[0] >= 'A' && normalized[0] <= 'Z') || (normalized[0] >= 'a' && normalized[0] <= 'z'))
        && normalized[1] == ':' && normalized[2] == '/';
#else
    return QDir::isAbsolutePath(path);
#endif
}

QString dataDirectory() {
    const QString overridePath = qEnvironmentVariable("STILL_DATA_DIR", qEnvironmentVariable("PIXCLONE_DATA_DIR"));
    if (!overridePath.isEmpty()) return QDir(overridePath).absolutePath();
    auto hasData = [](const QString &path) {
        return QFileInfo::exists(QDir(path).filePath("qt-settings.json"))
            || QFileInfo::exists(QDir(path).filePath("settings.json"))
            || QDir(QDir(path).filePath("History")).exists()
            || QFileInfo::exists(QDir(path).filePath("Clipboard/index.json"))
            || QFileInfo::exists(QDir(path).filePath("region-presets.json"))
            || QFileInfo::exists(QDir(path).filePath("PinSessions/session.json"));
    };
    QString currentPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QStringList legacyPaths;
#ifdef Q_OS_WIN
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!local.isEmpty()) {
        currentPath = QDir(local).filePath("Still");
        legacyPaths.append(QDir(local).filePath("PixClone"));
    }
    if (!roaming.isEmpty()) legacyPaths.append(QDir(roaming).filePath("PixClone"));
#else
    legacyPaths.append(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("PixClone/PixClone"));
#endif
    // Reuse existing data in place; changing the brand must not reset settings or history.
    if (hasData(currentPath)) return currentPath;
    for (const auto &path : legacyPaths) if (hasData(path)) return path;
    return currentPath;
}

QString settingsPath() { return QDir(dataDirectory()).filePath("qt-settings.json"); }

QString uniqueImagePath(const QString &directory, const QString &fileName) {
    const QFileInfo name(fileName.isEmpty() ? "still_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".png" : fileName);
    QString path = QDir(directory).filePath(name.fileName());
    for (int i = 2; QFileInfo::exists(path) || QFileInfo(path).isSymLink(); ++i)
        path = QDir(directory).filePath(name.completeBaseName() + '-' + QString::number(i) + '.' + name.suffix());
    return path;
}

bool saveImage(const QImage &image, const QString &path, QString *error, qint64 maximumBytes, int quality, int compression) {
    if (image.isNull()) { if (error) *error = t("图片为空"); return false; }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QByteArray format = (suffix == "jpg" || suffix == "jpeg") ? QByteArray("jpeg") : QByteArray("png");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) { if (error) *error = t("无法创建保存目录"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    QImageWriter writer(&file, format);
    writer.setQuality(quality);
    if (format == "png" && compression >= 0) writer.setCompression(compression);
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

} // namespace still
