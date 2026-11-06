#include "startup.h"
#include "ui/i18n.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

namespace still {

StartupStatus startupStatus() {
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            NSBundle *bundle = NSBundle.mainBundle;
            if (![[bundle objectForInfoDictionaryKey:@"CFBundlePackageType"] isEqualToString:@"APPL"] || !bundle.bundleIdentifier.length)
                return {StartupState::Unavailable, t("开机启动需要从应用包运行")};
            switch (SMAppService.mainAppService.status) {
            case SMAppServiceStatusNotRegistered:
                return {StartupState::Disabled, t("未登记开机启动")};
            case SMAppServiceStatusEnabled:
                return {StartupState::Enabled, t("已登记，登录时自动启动")};
            case SMAppServiceStatusRequiresApproval:
                return {StartupState::RequiresApproval, t("已登记，需在系统设置中允许；当前不会自动启动")};
            case SMAppServiceStatusNotFound:
                return {StartupState::NotFound, t("系统未找到开机启动登记，请重新登记或关闭")};
            }
        }
        return {StartupState::Unavailable, t("开机启动需要 macOS 13 或更新版本")};
    }
}

bool setStartupEnabled(bool enabled, QString *error) {
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            const auto status = startupStatus();
            if (status.state == StartupState::Unavailable) { *error = status.message; return false; }
            if ((enabled && status.state == StartupState::Enabled) || (!enabled && status.state == StartupState::Disabled)) return true;
            if (enabled && status.state == StartupState::RequiresApproval) { *error = status.message; return false; }
            NSError *failure = nil;
            SMAppService *service = SMAppService.mainAppService;
            const BOOL success = enabled ? [service registerAndReturnError:&failure] : [service unregisterAndReturnError:&failure];
            if (!success) *error = QString::fromUtf8(failure.localizedDescription.UTF8String);
            return success;
        }
        *error = t("开机启动需要 macOS 13 或更新版本");
        return false;
    }
}

void openStartupSettings() {
    if (@available(macOS 13.0, *)) [SMAppService openSystemSettingsLoginItems];
}

} // namespace still
