#include "image_export.h"
#include "store.h"
#include "ui/i18n.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace still {
namespace {
QString imageName(QString pattern, const QDateTime &when) {
    return pattern.replace("{date}", when.toString("yyyyMMdd"))
        .replace("{time}", when.toString("HHmmss")).replace("{ms}", when.toString("zzz"));
}
}

bool validImageNameTemplate(const QString &pattern) {
    const QString name = imageName(pattern.trimmed(), QDateTime(QDate(2026, 1, 1), QTime(0, 0)));
    static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*{}\\x00-\\x1f]"));
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
    return !name.isEmpty() && !name.endsWith('.') && !invalid.match(name).hasMatch() && !reserved.match(name).hasMatch();
}

QString imageExportFileName(const QJsonObject &options, const QDateTime &when) {
    const QString suffix = options.value("format").toString("png") == "jpeg" ? "jpg" : "png";
    return imageName(options.value("nameTemplate").toString(defaultImageNameTemplate), when) + '.' + suffix;
}

QString imageExportDirectory(const QJsonObject &options) {
    const QString directory = options.value("dir").toString();
    return directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : directory;
}

QString exportImage(const QImage &image, const QJsonObject &options, QString *error) {
    const QString directory = imageExportDirectory(options), name = imageExportFileName(options);
    if (!QDir().mkpath(directory)) { *error = t("无法创建保存目录"); return {}; }
    QFile target(uniqueImagePath(directory, name));
    // Reserve the name exclusively before QSaveFile replaces our own empty placeholder.
    while (!target.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (!QFileInfo::exists(target.fileName()) && !QFileInfo(target.fileName()).isSymLink()) { *error = target.errorString(); return {}; }
        target.setFileName(uniqueImagePath(directory, name));
    }
    target.close();
    if (!saveImage(image, target.fileName(), error, -1, options.value("jpegQuality").toInt(95), options.value("pngCompression").toInt(50))) {
        target.remove();
        return {};
    }
    return target.fileName();
}

} // namespace still
