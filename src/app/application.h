#pragma once

#include "platform/platform.h"
#include "imaging/pin_state.h"
#include "storage/region_presets.h"
#include <QJsonObject>
#include <QPointer>
#include <QRectF>
#include <QSystemTrayIcon>
#include <QTimer>

class QDialog;
class QAction;
template<class T> class QFutureWatcher;

namespace still {

class Editor;
class Overlay;
class Pin;
class PinWindow;
class TextPin;
class OcrJob;
class HistoryStore;
class SettingsApply;
class ScrollSession;
class ScrollCaptureUi;
class CaptureDelay;
class CapturePresets;
class ClipboardArchive;
class ClipboardController;
class MainWindow;
class PinSession;
struct PinSessionResult;

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject *parent = nullptr, bool monitorClipboard = true);
    ~Application() override;
    void openSettings();
    void capture(const QString &mode = "annotate");
    void openImage(const QString &path);
    void openClipboard();
private:
    SettingsApply *settingsApply_ = nullptr;
    HistoryStore *history_ = nullptr;
    ClipboardArchive *clipboardArchive_ = nullptr;
    ClipboardController *clipboard_ = nullptr;
    PinSession *pinSession_ = nullptr;
    QFutureWatcher<PinSessionResult> *pinRestoreJob_ = nullptr;
    QAction *restoreClosedAction_ = nullptr;
    QAction *repeatCaptureAction_ = nullptr;
    bool pinSessionTouched_ = false;
    bool pinSessionIncomplete_ = false;
    bool quitting_ = false;
    QVector<PinState> exitingPins_;
    Hotkeys hotkeys_;
    QSystemTrayIcon tray_;
    QVector<QPointer<PinWindow>> pins_;
    QVector<QPointer<Overlay>> overlays_;
    QVector<QPointer<QWidget>> hiddenForCapture_;
    QPointer<MainWindow> settingsWindow_;
    QPointer<OcrJob> ocrJob_;
    bool capturing_ = false;
    bool pinsHidden_ = false;
    ScrollSession *scroll_ = nullptr;
    QPointer<ScrollCaptureUi> scrollHud_;
    QPointer<CaptureDelay> captureDelay_;
    QPointer<CapturePresets> capturePresets_;
    QString regionPresetsPath_;
    QPointer<QScreen> lastCaptureScreen_;
    RegionPreset lastCaptureRegion_;
    QMetaObject::Connection scrollHudClosed_;
    QString scrollAction_;
    QString settingString(const QString &group, const QString &key, const QString &fallback = {}) const;
    void openMainWindow();
    void openDelayedCapture();
    void openCapturePresets();
    void beginCapture(const QString &mode, std::optional<RegionPreset> preset = std::nullopt);
    void rememberCaptureRegion(QScreen *screen, const RegionScreen &identity, const QRectF &region);
    int settingInt(const QString &group, const QString &key, int fallback) const;
    bool settingBool(const QString &group, const QString &key, bool fallback) const;
    void notify(const QString &message, bool error = false);
    void closeOverlays();
    void finishCapture();
    void deliverCapture(QImage image, QRect region, QString action);
    void route(const QImage &image, const QRect &region, const QString &action);
    Pin *createPin(const QImage &image, const QRect &region = {}, const PinState *restored = nullptr);
    TextPin *createTextPin(const QString &text, const QString &html = {}, const PinState *restored = nullptr);
    void attachPin(PinWindow *pin, const PinState *restored);
    Editor *createEditor(const QImage &image, Pin *target = nullptr);
    void remember(const QImage &image);
    bool copyImage(const QImage &image);
    void togglePins();
    void closePins();
    void pastePin();
    void startScroll(const QRect &region, const QImage &first);
    void finishScroll(const QImage &image, const QRect &region, const QString &message);
    void applyPinSettings();
    void restoreClosedPin();
    void restorePinSession();
    void updatePinRestoreActions();
    bool eventFilter(QObject *object, QEvent *event) override;
};

} // namespace still
