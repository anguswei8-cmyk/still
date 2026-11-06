#pragma once

#include <QImage>
#include <QWidget>

namespace still {

struct CaptureFrame { QImage image; QRect geometry; QVector<QRect> windows; };

class Overlay : public QWidget {
    Q_OBJECT
public:
    Overlay(CaptureFrame frame, QString mode, bool magnifier);
signals:
    void accepted(const QImage &image, const QRect &globalRegion, const QString &action);
    void cancelled();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;
private:
    CaptureFrame frame_;
    QString mode_;
    QRectF selection_;
    QRectF initialSelection_;
    QRect hovered_;
    QPointF start_;
    QPointF cursor_;
    bool dragging_ = false;
    bool finished_ = false;
    bool magnifier_ = true;
    int handle_ = -1;
    QWidget *toolbar_;
    QVector<QPointF> handles() const;
    void accept(const QString &action);
    void placeToolbar();
};

} // namespace still
