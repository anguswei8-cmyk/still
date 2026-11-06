#pragma once

#include "image.h"
#include <QObject>

namespace still {

class OcrJob : public QObject {
    Q_OBJECT
public:
    explicit OcrJob(const QImage &image, const QString &language, QObject *parent);
    ~OcrJob() override;
signals:
    void finished(const still::OcrResult &result);
private:
    QImage image_;
    QString language_;
    static void startPending();
};

} // namespace still
