#pragma once

#include "imaging/pin_state.h"
#include <QVector>
#include <optional>

namespace still {

struct PinSessionResult {
    QVector<PinState> pins;
    QStringList errors;
    bool limited = false;
};

class PinSession {
public:
    explicit PinSession(QString directory);
    void rememberClosed(PinState state);
    std::optional<PinState> takeClosed();
    bool canRestoreClosed() const { return !closed_.isEmpty(); }
    PinSessionResult load(const QStringList &exclude = {}, int maximum = 200) const;
    bool save(const QVector<PinState> &pins, QString *error) const;
private:
    QString directory_;
    QVector<PinState> closed_;
};

} // namespace still
