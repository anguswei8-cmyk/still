#pragma once

#include "imaging/image.h"
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QMap>
#include <QObject>

class QScreen;

namespace still {

OcrResult recognizeNative(const QImage &image, const QString &language);
QVector<QRect> windowRectangles(QScreen *screen);
bool requestCapturePermission();
bool excludeWindowFromCapture(quintptr windowId);
quintptr currentPasteTarget();
bool activatePasteTarget(quintptr target);
bool pasteToTarget(quintptr target, QString *error);

class Hotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys() override;
    bool bind(int id, const QKeySequence &sequence);
    void unbind(int id);
    bool stage(int id, const QKeySequence &sequence);
    void commitStaged();
    void discardStaged();
    QKeySequence sequence(int id) const { return sequences_.value(id); }
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;
    void dispatch(int id) { emit activated(id); }
    bool dispatchNative(int nativeId);
signals:
    void activated(int id);
private:
    QMap<int, quintptr> registrations_;
    QMap<int, QKeySequence> sequences_;
    QMap<int, int> nativeIds_;
    struct Reservation { QKeySequence sequence; quintptr handle = 0; int nativeId = 0; };
    QMap<int, Reservation> staged_;
    void *handler_ = nullptr;
    quintptr registerSequence(const QKeySequence &sequence, int nativeId);
    void releaseRegistration(quintptr handle);
};

} // namespace still
