#include "imaging/image.h"
#include <QThread>
#include <atomic>

std::atomic_bool ocrBlocked{false};
std::atomic_int ocrStarted{0};

namespace pixclone {
OcrResult recognizeNative(const QImage &image, const QString &) {
    ++ocrStarted;
    while (ocrBlocked) QThread::msleep(1);
    QThread::msleep(image.width() == 200 ? 120 : 5);
    if (image.width() == 200) return {{{"Old", QRectF(10, 10, 40, 20), 0}}, {}};
    return {{{"Hello", QRectF(10, 10, 40, 20), 0}, {"Qt", QRectF(60, 10, 40, 20), 0}, {"Next", QRectF(10, 50, 40, 20), 1}}, {}};
}
} // namespace pixclone
