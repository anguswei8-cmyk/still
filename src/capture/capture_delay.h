#pragma once

#include <QDeadlineTimer>
#include <QDialog>
#include <QTimer>

class QLabel;
class QPushButton;
class QSpinBox;

namespace still {

class CaptureDelay : public QDialog {
    Q_OBJECT
public:
    explicit CaptureDelay(QWidget *parent = nullptr);
protected:
    void done(int result) override;
private:
    QWidget *inputs_;
    QLabel *secondsLabel_;
    QSpinBox *seconds_;
    QLabel *countdown_;
    QPushButton *start_;
    QPushButton *cancel_;
    QDeadlineTimer deadline_;
    QTimer timer_;
    void updateText();
    void updateCountdown();
};

} // namespace still
