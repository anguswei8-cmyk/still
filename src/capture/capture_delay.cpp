#include "capture_delay.h"
#include "ui/i18n.h"

#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace still {

CaptureDelay::CaptureDelay(QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint), inputs_(new QWidget(this)),
      secondsLabel_(new QLabel(inputs_)), seconds_(new QSpinBox(inputs_)), countdown_(new QLabel(this)),
      start_(new QPushButton(this)), cancel_(new QPushButton(this)), timer_(this) {
    setObjectName("CaptureDelayDialog");
    setWindowModality(Qt::NonModal); setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    inputs_->setObjectName("DelayInputs");
    seconds_->setObjectName("DelaySeconds"); seconds_->setRange(1, 60); seconds_->setValue(5);
    secondsLabel_->setBuddy(seconds_);
    countdown_->setObjectName("DelayCountdown"); countdown_->hide();
    start_->setObjectName("DelayStart"); start_->setDefault(true);
    cancel_->setObjectName("DelayCancel");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout(inputs_); form->setContentsMargins(0, 0, 0, 0);
    form->addRow(secondsLabel_, seconds_); layout->addWidget(inputs_); layout->addWidget(countdown_);
    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(start_, QDialogButtonBox::AcceptRole); buttons->addButton(cancel_, QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    // Native Cocoa buttons and step arrows do not honor this dialog's dark palette.
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this);
    setStyle(style);
    for (auto *widget : findChildren<QWidget *>()) widget->setStyle(style);
    connect(start_, &QPushButton::clicked, this, [this] {
        deadline_.setRemainingTime(seconds_->value() * 1000, Qt::PreciseTimer);
        inputs_->hide(); start_->hide(); countdown_->show();
        timer_.start(100); updateCountdown(); adjustSize();
    });
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(&timer_, &QTimer::timeout, this, &CaptureDelay::updateCountdown);
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, &CaptureDelay::updateText);
    updateText();
}

void CaptureDelay::done(int result) {
    timer_.stop();
    QDialog::done(result);
}

void CaptureDelay::updateText() {
    setWindowTitle(t("延时截图"));
    secondsLabel_->setText(t("延时（秒）：")); seconds_->setAccessibleName(t("延时秒数"));
    start_->setText(t("开始倒计时")); cancel_->setText(t("取消"));
    if (timer_.isActive()) updateCountdown();
    adjustSize();
}

void CaptureDelay::updateCountdown() {
    const qint64 remaining = deadline_.remainingTime();
    if (remaining == 0) { accept(); return; }
    countdown_->setText(t("%1 秒后开始截图").arg((remaining + 999) / 1000));
}

} // namespace still
