#include "instance_request.h"

#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>

namespace still {

QByteArray encodeInstanceRequest(const QString &image, bool capture) {
    const QString path = image.isEmpty() ? QString() : QFileInfo(image).absoluteFilePath();
    return QJsonDocument(QJsonObject{{"image", path}, {"capture", capture}}).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<InstanceRequest> readInstanceRequest(QIODevice &input) {
    if (!input.canReadLine()) return std::nullopt;
    const auto document = QJsonDocument::fromJson(input.readLine());
    if (!document.isObject()) return std::nullopt;
    const auto object = document.object();
    if (!object.value("image").isString() || !object.value("capture").isBool()) return std::nullopt;
    return InstanceRequest{object.value("image").toString(), object.value("capture").toBool()};
}

} // namespace still
