#pragma once

#include "image.h"
#include <QObject>

namespace pixclone {

class OcrJob : public QObject {
    Q_OBJECT
public:
    explicit OcrJob(const QImage &image, const QString &language, QObject *parent);
    ~OcrJob() override;
signals:
    void finished(const pixclone::OcrResult &result);
private:
    QImage image_;
    QString language_;
    static void startPending();
};

} // namespace pixclone
