#pragma once

#include <QImage>
#include <QRect>
#include <QString>

namespace still {

inline constexpr qint64 maximumTextPinBytes = 32LL * 1024 * 1024;

struct PinState {
    QString id;
    QImage image;
    QRect geometry;
    QSize originalSize;
    qreal opacity = 1;
    bool alwaysOnTop = true;
    QString text;
    QString html = {};
};

} // namespace still
