#pragma once

#include "startup.h"

namespace still::startup_task {
struct Inspection {
    bool owned = false;
    StartupStatus status;
};
QString definition(const QString &executablePath, const QString &sid);
Inspection inspect(const QString &xml, const QString &executablePath, const QString &sid, bool enabled);
} // namespace still::startup_task
