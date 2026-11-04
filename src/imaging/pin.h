#pragma once

#include "image.h"
#include <QPointer>
#include <QWidget>

namespace pixclone {

class OcrJob;

class Pin : public QWidget {
    Q_OBJECT
public:
    Pin(const QImage &image, const QRect &geometry, const QString &saveDirectory, const QString &language);
    void setImage(const QImage &image);
    void recognize();
    void configure(bool wheelZoom, bool hideOnBlur, bool confirmDoubleClickClose = true);
    QImage image() const { return image_; }
signals:
    void editRequested(pixclone::Pin *pin);
    void toggleAllRequested();
    void closeAllRequested();
    void doubleClickClosePromptDisabled();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    bool event(QEvent *) override;
private:
    QImage image_;
    QVector<OcrWord> words_;
    QString saveDirectory_;
    QString language_;
    QString status_;
    QPointer<OcrJob> ocrJob_;
    int generation_ = 0;
    int anchor_ = -1;
    int cursor_ = -1;
    bool selecting_ = false;
    bool moving_ = false;
    bool wheelZoom_ = true;
    bool hideOnBlur_ = false;
    bool confirmDoubleClickClose_ = true;
    bool confirmingClose_ = false;
    QPoint dragOffset_;
    QSize originalSize_;
    QRectF imageRect() const;
    QPointF imagePoint(const QPointF &point) const;
    void copy(bool forceImage = false);
    void zoom(qreal factor, const QPoint &anchor);
};

} // namespace pixclone
