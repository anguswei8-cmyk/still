#include "platform/platform.h"
#include "platform/startup.h"
#include "ui/i18n.h"
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>

namespace still {
static StartupStatus startup{StartupState::Disabled, {}};
static QString startupFailure;
static bool startupChangesBeforeFailure = false;
static int startupCalls = 0, startupSettingsOpened = 0;
void setStartupStatusForTest(StartupState state, const QString &message, const QString &settingsLabel) { startup = {state, message, settingsLabel}; }
void setStartupFailureForTest(const QString &message, bool changesState) { startupFailure = message; startupChangesBeforeFailure = changesState; }
int startupChangeCountForTest() { return startupCalls; }
int startupSettingsOpenedForTest() { return startupSettingsOpened; }
StartupStatus startupStatus() {
    auto status = startup;
    status.message = InterfacePreferences::instance().translatedCaption(status.message);
    status.settingsLabel = InterfacePreferences::instance().translatedCaption(status.settingsLabel);
    return status;
}
bool setStartupEnabled(bool enabled, QString *error) {
    ++startupCalls;
    if (!startupFailure.isEmpty()) {
        if (startupChangesBeforeFailure) startup.state = enabled ? StartupState::Enabled : StartupState::Disabled;
        *error = startupFailure; return false;
    }
    startup = {enabled ? StartupState::Enabled : StartupState::Disabled, {}, startup.settingsLabel};
    return true;
}
void openStartupSettings() { ++startupSettingsOpened; }
bool excludeWindowFromCapture(quintptr) { return false; }
static quintptr pasteTarget = 1;
static quintptr lastPasteTarget = 0;
void setPasteTargetForTest(quintptr target) { pasteTarget = target; lastPasteTarget = 0; }
quintptr lastPasteTargetForTest() { return lastPasteTarget; }
quintptr currentPasteTarget() { return pasteTarget; }
bool activatePasteTarget(quintptr target) { lastPasteTarget = target; return false; }
bool pasteToTarget(quintptr, QString *error) { *error = "test target unavailable"; return false; }
static QMutex ocrMutex;
static OcrResult ocrResult;
static int ocrCalls = 0;
void setOcrResultForTest(const OcrResult &result) { const QMutexLocker lock(&ocrMutex); ocrResult = result; }
int ocrCallCountForTest() { const QMutexLocker lock(&ocrMutex); return ocrCalls; }
OcrResult recognizeNative(const QImage &, const QString &) { const QMutexLocker lock(&ocrMutex); ++ocrCalls; return ocrResult; }
QVector<QRect> windowRectangles(QScreen *) { return {}; }
bool requestCapturePermission() { return true; }
static int capturedScreens = 0;
static QImage capturedImage;
void resetCapturedScreensForTest() { capturedScreens = 0; }
int capturedScreenCountForTest() { return capturedScreens; }
void setCapturedImageForTest(const QImage &image) { capturedImage = image; }
QImage captureScreen(QScreen *screen) {
    ++capturedScreens;
    if (!capturedImage.isNull()) return capturedImage.copy();
    QImage image(screen->size() * screen->devicePixelRatio(), QImage::Format_ARGB32);
    image.fill(QColor("#3975dc"));
    return image;
}
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
