#pragma once

#include "clipboard_archive.h"
#include <QElapsedTimer>
#include <QPointer>

class QWidget;

namespace still {

enum class ClipAction { Paste, Copy, PlainPaste, EditImage, PinImage, SaveImage, PinText };

class ClipboardController : public QObject {
    Q_OBJECT
public:
    ClipboardController(ClipboardArchive &archive, QJsonObject options, bool monitor, QObject *parent = nullptr);
    void attachHost(QWidget *host);
    void prepare();
    void configure(const QJsonObject &options);
    void cancel();
    void setPaused(bool paused);
    void activate(const QString &id, ClipAction action);
    bool paused() const { return paused_ || !enabled_; }
    bool pasteDefault() const { return pasteDefault_ && target_ != 0; }
signals:
    void settingsRequested();
    void dismissRequested();
    void message(const QString &text);
    void feedback(const QString &text);
    void stateChanged();
    void editImage(const QImage &image);
    void pinImage(const QImage &image);
    void pinText(const QString &text, const QString &html = {});
    void saveImage(const QImage &image);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    ClipboardArchive &archive_;
    QPointer<QWidget> host_;
    bool enabled_ = true;
    bool paused_ = false;
    bool pasteDefault_ = true;
    bool writing_ = false;
    bool capturing_ = false;
    QString pendingId_;
    ClipAction pendingAction_ = ClipAction::Copy;
    quintptr target_ = 0;
    quint64 generation_ = 0;
    QByteArray restoreToken_;
    QElapsedTimer restoreTime_;
    void capture();
};

} // namespace still

Q_DECLARE_METATYPE(still::ClipAction)
