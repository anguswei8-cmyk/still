#pragma once

#include "pin_state.h"
#include <QWidget>

namespace still {

class PinWindow : public QWidget {
    Q_OBJECT
public:
    PinWindow();
    virtual PinState state() const = 0;
    void restoreLayout(const PinState &state);
    void setRestoreAvailable(bool available) { canRestoreClosed_ = available; }
    void setHideOnBlur(bool enabled) { hideOnBlur_ = enabled; }
signals:
    void closing(const still::PinState &state);
    void toggleAllRequested();
    void closeAllRequested();
    void restoreClosedRequested();
protected:
    PinState layoutState() const;
    void closeEvent(QCloseEvent *) override;
    bool event(QEvent *) override;
    QSize originalSize_;
    bool canRestoreClosed_ = false;
    bool confirmingClose_ = false;
private:
    QString id_;
    bool hideOnBlur_ = false;
};

} // namespace still
