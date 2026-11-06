#pragma once

#include "startup.h"

namespace still::startup_windows {
struct Identity {
    QString taskName;
    QString executablePath;
};

QString currentUserSid(QString *error);
StartupStatus status(const Identity &identity);
bool setEnabled(const Identity &identity, bool enabled, QString *error);
} // namespace still::startup_windows
