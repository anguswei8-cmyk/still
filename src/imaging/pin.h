#pragma once

#include "image.h"
#include "pin_window.h"
#include <QPointer>
#include <QJsonObject>
#include <QWidget>

namespace still {

class OcrJob;

class Pin : public PinWindow {
    Q_OBJECT
public:
    Pin(const QImage &image, const QRect &geometry, const QJsonObject &saveOptions, const QString &language);
    void configureSave(const QJsonObject &options) { saveOptions_ = options; }
    void setImage(const QImage &image);
    void recognize();
    void configure(bool wheelZoom, bool hideOnBlur, bool confirmDoubleClickClose = true);
    QImage image() const { return image_; }
    PinState state() const override;
signals:
    void editRequested(still::Pin *pin);
    void doubleClickClosePromptDisabled();
    void imageSaved(const QImage &image);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
private:
    QImage image_;
    OcrResult ocr_;
    QJsonObject saveOptions_;
    QString language_;
    QString status_;
    QPointer<OcrJob> ocrJob_;
    int generation_ = 0;
    int anchor_ = -1;
    int cursor_ = -1;
    bool allTextSelected_ = false;
    bool selecting_ = false;
    bool moving_ = false;
    bool wheelZoom_ = true;
    bool confirmDoubleClickClose_ = true;
    QPoint dragOffset_;
    QRectF imageRect() const;
    QPointF imagePoint(const QPointF &point) const;
    void copy(bool forceImage = false);
    void save(bool quick = false);
    void zoom(qreal factor, const QPoint &anchor);
};

} // namespace still
