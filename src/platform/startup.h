#pragma once

#include <QString>

namespace still {
enum class StartupState { Unavailable, Disabled, Enabled, RequiresApproval, NotFound };
struct StartupStatus {
    StartupState state = StartupState::Unavailable;
    QString message;
    QString settingsLabel = {};
};
StartupStatus startupStatus();
bool setStartupEnabled(bool enabled, QString *error);
void openStartupSettings();
} // namespace still
