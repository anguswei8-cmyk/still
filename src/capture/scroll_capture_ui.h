#pragma once

#include <QImage>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace still {

class ScrollCaptureUi : public QWidget {
    Q_OBJECT
public:
    ScrollCaptureUi(const QRect &region, const QRect &available, QWidget *parent = nullptr);
    bool prepare();
    void setImage(const QImage &image);
    void showAlignmentPending();
signals:
    void finishRequested();
    void actionRequested(const QString &action);
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    QRect region_, available_;
    QScrollArea *previewArea_;
    QLabel *preview_;
    QWidget *frame_ = nullptr;
    QWidget *toolbar_;
    QLabel *dimensions_;
    int viewportHeight_ = 0;
    int previewMaximumHeight_;
    bool followPreview_ = true;
};

} // namespace still
