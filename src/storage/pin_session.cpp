#include "pin_session.h"
#include "store.h"
#include "ui/i18n.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <limits>

namespace still {
namespace {
bool integer(const QJsonValue &value) {
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    return std::isfinite(number) && number == std::floor(number)
        && number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max();
}
bool validRecord(const QJsonObject &object) {
    static const QRegularExpression imagePath("^session-[A-Za-z0-9]{6}/[0-9]+\\.png$");
    static const QRegularExpression textPath("^session-[A-Za-z0-9]{6}/[0-9]+\\.txt$");
    const QString kind = object.value("kind").toString();
    const QString text = object.value("text").toString();
    const bool validContent = kind == "image" ? imagePath.match(object.value("image").toString()).hasMatch()
        : (kind == "text" || kind == "html") && textPath.match(text).hasMatch()
            && (kind == "text" || object.value("html").toString() == text.chopped(4) + ".html");
    const auto geometry = object.value("geometry").toArray(), original = object.value("originalSize").toArray();
    if (geometry.size() != 4 || original.size() != 2) return false;
    for (const auto &number : geometry) if (!integer(number)) return false;
    for (const auto &number : original) if (!integer(number)) return false;
    const auto opacity = object.value("opacity");
    return !QUuid(object.value("id").toString()).isNull() && validContent
        && geometry[2].toInt() > 0 && geometry[3].toInt() > 0 && original[0].toInt() > 0 && original[1].toInt() > 0
        && qint64(geometry[0].toInt()) + geometry[2].toInt() <= std::numeric_limits<int>::max()
        && qint64(geometry[1].toInt()) + geometry[3].toInt() <= std::numeric_limits<int>::max()
        && opacity.isDouble() && opacity.toDouble() >= 0.15 && opacity.toDouble() <= 1 && object.value("alwaysOnTop").isBool();
}
std::optional<QJsonArray> readIndex(const QString &directory, QString *error) {
    QFile file(QDir(directory).filePath("session.json"));
    if (!file.exists()) return QJsonArray{};
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return std::nullopt; }
    const auto invalid = [&] {
        *error = t("贴图会话索引损坏或版本不支持，原文件已保留：") + file.fileName();
        return std::optional<QJsonArray>{};
    };
    if (file.size() > 1024 * 1024) return invalid();
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto object = document.object();
    const auto version = object.value("version");
    if ((version != 1 && version != 2 && version != 3) || !object.value("pins").isArray()) return invalid();
    auto records = object.value("pins").toArray();
    if (records.size() > 200) return invalid();
    QSet<QString> ids;
    for (auto value : records) {
        auto record = value.toObject();
        if (version == 1) record["kind"] = "image";
        const QString id = record.value("id").toString();
        if (!validRecord(record) || (version != 3 && record.value("kind") == "html") || ids.contains(id)) return invalid();
        ids.insert(id);
        value = record;
    }
    return records;
}
QString contentPath(const QJsonObject &record) {
    return record.value(record.value("kind") == "image" ? "image" : "text").toString();
}
std::optional<QString> readText(const QString &path, qint64 &remaining, QString *error) {
    const auto failed = [&](const QString &reason) {
        *error = t("无法恢复贴图：") + path + " — " + reason;
        return std::optional<QString>{};
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return failed(file.errorString());
    if (file.size() > remaining) return failed(t("文本贴图内容无效或超过 32 MiB"));
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return failed(file.errorString());
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString text = decoder(bytes);
    if (decoder.hasError() || text.isEmpty()) return failed(t("文本贴图内容无效或超过 32 MiB"));
    remaining -= bytes.size();
    return text;
}
bool saveText(const QString &text, const QString &path, qint64 &remaining, QString *error) {
    QStringEncoder encoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QByteArray bytes = encoder(text);
    if (text.isEmpty() || encoder.hasError() || bytes.size() > remaining) { *error = t("文本贴图内容无效或超过 32 MiB"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { *error = file.errorString(); return false; }
    remaining -= bytes.size();
    return true;
}
qint64 contentBytes(const PinState &state) {
    return state.image.sizeInBytes() + (state.text.size() + state.html.size()) * qint64(sizeof(QChar));
}
}

PinSession::PinSession(QString directory) : directory_(std::move(directory)) {}

void PinSession::rememberClosed(PinState state) {
    closed_.append(std::move(state));
    qint64 bytes = 0;
    for (const auto &pin : closed_) bytes += contentBytes(pin);
    // The close stack owns image pixels and text independently of screenshot-history retention.
    while (closed_.size() > 1 && (closed_.size() > 20 || bytes > 256LL * 1024 * 1024)) {
        bytes -= contentBytes(closed_.first());
        closed_.removeFirst();
    }
}

std::optional<PinState> PinSession::takeClosed() {
    if (closed_.isEmpty()) return std::nullopt;
    return closed_.takeLast();
}

PinSessionResult PinSession::load(const QStringList &exclude, int maximum) const {
    PinSessionResult result;
    QString error;
    const auto records = readIndex(directory_, &error);
    if (!records) { result.errors.append(error); return result; }
    for (const auto &value : *records) {
        const auto record = value.toObject();
        if (exclude.contains(record.value("id").toString())) continue;
        if (result.pins.size() >= maximum) { result.limited = true; break; }
        const QString path = QDir(directory_).filePath(contentPath(record));
        QImage image;
        QString text, html;
        if (record.value("kind") != "image") {
            qint64 remaining = maximumTextPinBytes;
            const auto plain = readText(path, remaining, &error);
            if (!plain) { result.errors.append(error); continue; }
            text = *plain;
            if (record.value("kind") == "html") {
                const auto rich = readText(QDir(directory_).filePath(record.value("html").toString()), remaining, &error);
                if (!rich) { result.errors.append(error); continue; }
                html = *rich;
            }
        } else {
            QImageReader reader(path, "PNG");
            image = reader.read();
            if (image.isNull()) { result.errors.append(t("无法恢复贴图：") + path + " — " + reader.errorString()); continue; }
        }
        const auto geometry = record.value("geometry").toArray(), original = record.value("originalSize").toArray();
        result.pins.append({record.value("id").toString(), image,
            QRect(geometry[0].toInt(), geometry[1].toInt(), geometry[2].toInt(), geometry[3].toInt()),
            QSize(original[0].toInt(), original[1].toInt()), record.value("opacity").toDouble(), record.value("alwaysOnTop").toBool(), text, html});
    }
    return result;
}

bool PinSession::save(const QVector<PinState> &pins, QString *error) const {
    const auto previous = readIndex(directory_, error);
    if (!previous) return false;
    if (!QDir().mkpath(directory_)) { *error = t("无法创建贴图会话目录"); return false; }
    QTemporaryDir next(QDir(directory_).filePath("session-XXXXXX"));
    if (!next.isValid()) { *error = next.errorString(); return false; }
    const QString folder = QFileInfo(next.path()).fileName();
    QJsonArray records;
    for (const auto &pin : pins) {
        const bool text = pin.image.isNull() && (!pin.text.isEmpty() || !pin.html.isEmpty());
        const QString kind = text ? (pin.html.isEmpty() ? "text" : "html") : "image";
        const QString number = QString::number(records.size()), name = number + (text ? ".txt" : ".png");
        const QString path = QDir(next.path()).filePath(name);
        if (text) {
            qint64 remaining = maximumTextPinBytes;
            if (!saveText(pin.text, path, remaining, error)) return false;
            if (kind == "html" && !saveText(pin.html, QDir(next.path()).filePath(number + ".html"), remaining, error)) return false;
        } else if (!saveImage(pin.image, path, error)) return false;
        QJsonObject record{{"id", pin.id}, {"kind", kind}, {text ? "text" : "image", folder + '/' + name},
            {"geometry", QJsonArray{pin.geometry.x(), pin.geometry.y(), pin.geometry.width(), pin.geometry.height()}},
            {"originalSize", QJsonArray{pin.originalSize.width(), pin.originalSize.height()}},
            {"opacity", pin.opacity}, {"alwaysOnTop", pin.alwaysOnTop}};
        if (kind == "html") record["html"] = folder + '/' + number + ".html";
        records.append(record);
    }
    if (!writeJson(QDir(directory_).filePath("session.json"), {{"version", 3}, {"pins", records}}, error)) return false;
    if (!records.isEmpty()) next.setAutoRemove(false);
    QSet<QString> obsolete;
    for (const auto &record : *previous) obsolete.insert(contentPath(record.toObject()).section('/', 0, 0));
    for (const auto &name : obsolete) QDir(QDir(directory_).filePath(name)).removeRecursively();
    return true;
}

} // namespace still
