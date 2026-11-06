#include "ui/i18n.h"
#include "platform.h"
#include <QScreen>
#include <QPixmap>

namespace still {
quintptr currentPasteTarget() { return 0; }
bool excludeWindowFromCapture(quintptr) { return false; }
bool activatePasteTarget(quintptr) { return false; }
bool pasteToTarget(quintptr, QString *error) { *error = t("当前平台请手动粘贴"); return false; }
OcrResult recognizeNative(const QImage &, const QString &) { return {{}, {}, t("此版本的系统 OCR 支持 Windows 和 macOS")}; }
QVector<QRect> windowRectangles(QScreen *) { return {}; }
bool requestCapturePermission() { return true; }
QImage captureScreen(QScreen *screen) { return screen->grabWindow(0).toImage(); }
Hotkeys::Hotkeys(QObject *parent) : QObject(parent) {}
Hotkeys::~Hotkeys() = default;
quintptr Hotkeys::registerSequence(const QKeySequence &, int) { return 0; }
void Hotkeys::releaseRegistration(quintptr) {}
bool Hotkeys::nativeEventFilter(const QByteArray &, void *, qintptr *) { return false; }
} // namespace still
