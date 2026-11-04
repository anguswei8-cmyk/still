#pragma once

#include "platform/platform.h"
#include <QJsonObject>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>

class QDialog;

namespace pixclone {

class Editor;
class Overlay;
class Pin;
class OcrJob;
class HistoryStore;
class SettingsApply;
class ScrollSession;
class ScrollCaptureUi;
class ClipboardArchive;
class ClipboardController;
class MainWindow;

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
    Hotkeys hotkeys_;
    QSystemTrayIcon tray_;
    QVector<QPointer<Pin>> pins_;
    QVector<QPointer<Overlay>> overlays_;
    QVector<QPointer<QWidget>> hiddenForCapture_;
    QPointer<MainWindow> settingsWindow_;
    QPointer<OcrJob> ocrJob_;
    bool capturing_ = false;
    bool pinsHidden_ = false;
    ScrollSession *scroll_ = nullptr;
    QPointer<ScrollCaptureUi> scrollHud_;
    QMetaObject::Connection scrollHudClosed_;
    QString scrollAction_;
    QString settingString(const QString &group, const QString &key, const QString &fallback = {}) const;
    void openMainWindow();
    int settingInt(const QString &group, const QString &key, int fallback) const;
    bool settingBool(const QString &group, const QString &key, bool fallback) const;
    void notify(const QString &message, bool error = false);
    void closeOverlays();
    void finishCapture();
    void route(const QImage &image, const QRect &region, const QString &action);
    Pin *createPin(const QImage &image, const QRect &region = {});
    Editor *createEditor(const QImage &image);
    void remember(const QImage &image);
    void togglePins();
    void closePins();
    void pastePin();
    void startScroll(const QRect &region, const QImage &first);
    void finishScroll(const QImage &image, const QRect &region, const QString &message);
    void applyPinSettings();
};

} // namespace pixclone
