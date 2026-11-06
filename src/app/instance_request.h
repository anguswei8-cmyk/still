#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

class QIODevice;

namespace still {

struct InstanceRequest {
    QString image;
    bool capture = false;
};

QByteArray encodeInstanceRequest(const QString &image, bool capture);
std::optional<InstanceRequest> readInstanceRequest(QIODevice &input);

} // namespace still
