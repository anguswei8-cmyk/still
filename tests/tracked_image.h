#pragma once

#include <QImage>
#include <atomic>

inline std::atomic_int liveTrackedImages{0};

inline QImage trackedImage(int width = 320, int height = 180) {
    auto *bytes = new uchar[size_t(width) * height * 4];
    ++liveTrackedImages;
    QImage image(bytes, width, height, QImage::Format_ARGB32, [](void *data) {
        delete[] static_cast<uchar *>(data);
        --liveTrackedImages;
    }, bytes);
    image.fill(Qt::white);
    return image;
}
