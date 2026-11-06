#include "startup.h"
#include "ui/i18n.h"

namespace still {
StartupStatus startupStatus() { return {StartupState::Unavailable, t("当前版本尚未支持此平台的开机启动")}; }
bool setStartupEnabled(bool, QString *error) { *error = startupStatus().message; return false; }
void openStartupSettings() {}
} // namespace still
