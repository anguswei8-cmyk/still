#include "imaging/image.h"
#include <QThread>
#include <atomic>

std::atomic_bool ocrBlocked{false};
std::atomic_int ocrStarted{0};

namespace still {
OcrResult recognizeNative(const QImage &image, const QString &) {
    ++ocrStarted;
    while (ocrBlocked) QThread::msleep(1);
    QThread::msleep(image.width() == 200 ? 120 : 5);
    if (image.width() == 420) return {"  Wide  \tQt\r\nNext!  ", {{QRectF(10, 10, 40, 20), 2, 4}, {QRectF(60, 10, 40, 20), 9, 2}, {QRectF(10, 50, 40, 20), 13, 5}}, {}};
    if (image.width() == 440) return {"wide short", {{QRectF(0, 0, 200, 20), 0, 4}, {QRectF(205, 30, 10, 10), 5, 5}}, {}};
    if (image.width() == 200) return {"Old", {{QRectF(10, 10, 40, 20), 0, 3}}, {}};
    return {"Hello Qt\nNext", {{QRectF(10, 10, 40, 20), 0, 5}, {QRectF(60, 10, 40, 20), 6, 2}, {QRectF(10, 50, 40, 20), 9, 4}}, {}};
}
} // namespace still
