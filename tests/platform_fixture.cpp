#include "platform/platform.h"
#include <QScreen>

namespace still {
bool excludeWindowFromCapture(quintptr) { return false; }
static quintptr pasteTarget = 1;
static quintptr lastPasteTarget = 0;
void setPasteTargetForTest(quintptr target) { pasteTarget = target; lastPasteTarget = 0; }
quintptr lastPasteTargetForTest() { return lastPasteTarget; }
quintptr currentPasteTarget() { return pasteTarget; }
bool activatePasteTarget(quintptr target) { lastPasteTarget = target; return false; }
bool pasteToTarget(quintptr, QString *error) { *error = "test target unavailable"; return false; }
OcrResult recognizeNative(const QImage &, const QString &) { return {}; }
QVector<QRect> windowRectangles(QScreen *) { return {}; }
bool requestCapturePermission() { return true; }
Hotkeys::Hotkeys(QObject *parent) : QObject(parent) {}
Hotkeys::~Hotkeys() { discardStaged(); for (int id : registrations_.keys()) unbind(id); }
static QMap<quintptr, QKeySequence> claims;
quintptr Hotkeys::registerSequence(const QKeySequence &sequence, int nativeId) {
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_F12 || claims.values().contains(sequence)) return 0;
    claims[quintptr(nativeId)] = sequence;
    return quintptr(nativeId);
}
void Hotkeys::releaseRegistration(quintptr handle) { claims.remove(handle); }
int nativeIdForTest(const QKeySequence &sequence) { return int(claims.key(sequence, 0)); }
bool Hotkeys::nativeEventFilter(const QByteArray &, void *, qintptr *) { return false; }
} // namespace still
