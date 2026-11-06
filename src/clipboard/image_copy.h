#pragma once

#include <QImage>
#include <QJsonObject>

namespace still {

bool copyImageToClipboard(const QImage &image, const QJsonObject &saveOptions, QString *error);

} // namespace still
