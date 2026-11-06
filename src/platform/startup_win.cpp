#include "startup_win.h"
#include "startup_task.h"
#include "ui/i18n.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QScopeGuard>
#include <memory>

namespace still {
namespace {
using Bstr = std::unique_ptr<OLECHAR, decltype(&SysFreeString)>;
Bstr bstr(const QString &value) {
    return {SysAllocStringLen(reinterpret_cast<const OLECHAR *>(value.utf16()), UINT(value.size())), SysFreeString};
}

QString windowsError(HRESULT result) {
    wchar_t *message = nullptr;
    auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, DWORD(result), 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    if (!length && HRESULT_FACILITY(result) == FACILITY_WIN32)
        length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, HRESULT_CODE(result), 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    const auto release = qScopeGuard([&] { if (message) LocalFree(message); });
    const QString detail = length ? QString::fromWCharArray(message, int(length)).trimmed() : t("Windows 未提供此错误的说明");
    return t("Windows 任务计划程序：") + QStringLiteral("0x%1 — ").arg(quint32(result), 8, 16, QLatin1Char('0')) + detail;
}

StartupStatus managedStatus(StartupStatus status) {
    status.settingsLabel = t("任务计划程序…");
    return status;
}

struct Scheduler {
    HRESULT result;
    bool initialized;
    ITaskService *service = nullptr;
    ITaskFolder *folder = nullptr;

    Scheduler() : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), initialized(SUCCEEDED(result)) {
        // Qt may already have initialized this thread in another COM apartment.
        if (FAILED(result) && result != RPC_E_CHANGED_MODE) return;
        result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                  reinterpret_cast<void **>(&service));
        if (FAILED(result)) return;
        const VARIANT empty{};
        result = service->Connect(empty, empty, empty, empty);
        if (FAILED(result)) return;
        const auto root = bstr(QStringLiteral("\\"));
        result = root ? service->GetFolder(root.get(), &folder) : E_OUTOFMEMORY;
    }
    ~Scheduler() {
        if (folder) folder->Release();
        if (service) service->Release();
        if (initialized) CoUninitialize();
    }
};

bool taskMissing(HRESULT result) {
    return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

HRESULT readTask(ITaskFolder *folder, const startup_windows::Identity &identity, const QString &sid,
                 IRegisteredTask **task, startup_task::Inspection *inspection) {
    const auto name = bstr(identity.taskName);
    if (!name) return E_OUTOFMEMORY;
    HRESULT result = folder->GetTask(name.get(), task);
    if (taskMissing(result)) {
        inspection->status = {StartupState::Disabled, t("未在任务计划程序登记登录启动")};
        return S_FALSE;
    }
    if (FAILED(result)) return result;
    VARIANT_BOOL enabled = VARIANT_FALSE;
    result = (*task)->get_Enabled(&enabled);
    if (FAILED(result)) return result;
    BSTR xml = nullptr;
    result = (*task)->get_Xml(&xml);
    const auto release = qScopeGuard([&] { SysFreeString(xml); });
    if (FAILED(result)) return result;
    *inspection = startup_task::inspect(QString::fromWCharArray(xml, int(SysStringLen(xml))), identity.executablePath, sid,
                                        enabled != VARIANT_FALSE);
    return S_OK;
}

StartupStatus readStatus(ITaskFolder *folder, const startup_windows::Identity &identity, const QString &sid) {
    IRegisteredTask *task = nullptr;
    const auto release = qScopeGuard([&] { if (task) task->Release(); });
    startup_task::Inspection inspection;
    const HRESULT result = readTask(folder, identity, sid, &task, &inspection);
    return managedStatus(FAILED(result) ? StartupStatus{StartupState::Unavailable, windowsError(result)} : inspection.status);
}

startup_windows::Identity productionIdentity(const QString &sid) {
    return {QStringLiteral("Still.LoginStartup.") + sid, QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
}
} // namespace

namespace startup_windows {
QString currentUserSid(QString *error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        *error = windowsError(HRESULT_FROM_WIN32(GetLastError()));
        return {};
    }
    const auto close = qScopeGuard([&] { CloseHandle(token); });
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    const DWORD informationError = GetLastError();
    if (informationError != ERROR_INSUFFICIENT_BUFFER) {
        *error = windowsError(HRESULT_FROM_WIN32(informationError));
        return {};
    }
    QByteArray information(int(size), Qt::Uninitialized);
    if (!GetTokenInformation(token, TokenUser, information.data(), size, &size)) {
        *error = windowsError(HRESULT_FROM_WIN32(GetLastError()));
        return {};
    }
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(information.data())->User.Sid, &sid)) {
        *error = windowsError(HRESULT_FROM_WIN32(GetLastError()));
        return {};
    }
    const auto release = qScopeGuard([&] { LocalFree(sid); });
    return QString::fromWCharArray(sid);
}

StartupStatus status(const Identity &identity) {
    QString error;
    const auto sid = currentUserSid(&error);
    if (sid.isEmpty()) return managedStatus({StartupState::Unavailable, error});
    Scheduler scheduler;
    if (FAILED(scheduler.result)) return managedStatus({StartupState::Unavailable, windowsError(scheduler.result)});
    return readStatus(scheduler.folder, identity, sid);
}

bool setEnabled(const Identity &identity, bool enabled, QString *error) {
    const auto sid = currentUserSid(error);
    if (sid.isEmpty()) return false;
    Scheduler scheduler;
    if (FAILED(scheduler.result)) { *error = windowsError(scheduler.result); return false; }
    IRegisteredTask *existing = nullptr;
    const auto releaseExisting = qScopeGuard([&] { if (existing) existing->Release(); });
    startup_task::Inspection inspection;
    HRESULT result = readTask(scheduler.folder, identity, sid, &existing, &inspection);
    if (FAILED(result)) { *error = windowsError(result); return false; }
    const bool exists = result != S_FALSE;
    if (exists && !inspection.owned) { *error = inspection.status.message; return false; }
    if (!enabled && !exists) return true;
    if (enabled && exists && inspection.status.state == StartupState::Enabled) return true;
    const auto name = bstr(identity.taskName);
    if (!name) { *error = windowsError(E_OUTOFMEMORY); return false; }
    if (!enabled) {
        result = scheduler.folder->DeleteTask(name.get(), 0);
    } else {
        ITaskDefinition *definition = nullptr;
        const auto releaseDefinition = qScopeGuard([&] { if (definition) definition->Release(); });
        result = scheduler.service->NewTask(0, &definition);
        if (FAILED(result)) { *error = windowsError(result); return false; }
        const auto xml = bstr(startup_task::definition(identity.executablePath, sid));
        if (!xml) { *error = windowsError(E_OUTOFMEMORY); return false; }
        result = definition->put_XmlText(xml.get());
        if (FAILED(result)) { *error = windowsError(result); return false; }
        VARIANT user{};
        user.vt = VT_BSTR;
        user.bstrVal = SysAllocStringLen(reinterpret_cast<const OLECHAR *>(sid.utf16()), UINT(sid.size()));
        const auto clearUser = qScopeGuard([&] { VariantClear(&user); });
        if (!user.bstrVal) { *error = windowsError(E_OUTOFMEMORY); return false; }
        const VARIANT empty{};
        IRegisteredTask *registered = nullptr;
        const auto releaseRegistered = qScopeGuard([&] { if (registered) registered->Release(); });
        // TASK_CREATE refuses a concurrent name collision; updates never delete the previous task first.
        result = scheduler.folder->RegisterTaskDefinition(name.get(), definition, exists ? TASK_UPDATE : TASK_CREATE,
                                                          user, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    }
    const auto current = readStatus(scheduler.folder, identity, sid);
    if (result != S_OK) { *error = windowsError(result) + QLatin1Char('\n') + current.message; return false; }
    const auto expected = enabled ? StartupState::Enabled : StartupState::Disabled;
    if (current.state != expected) {
        *error = t("操作已提交，但任务计划程序尚未确认目标状态：") + current.message;
        return false;
    }
    return true;
}
} // namespace startup_windows

StartupStatus startupStatus() {
    QString error;
    const auto sid = startup_windows::currentUserSid(&error);
    return sid.isEmpty() ? managedStatus({StartupState::Unavailable, error}) : startup_windows::status(productionIdentity(sid));
}

bool setStartupEnabled(bool enabled, QString *error) {
    const auto sid = startup_windows::currentUserSid(error);
    return !sid.isEmpty() && startup_windows::setEnabled(productionIdentity(sid), enabled, error);
}

void openStartupSettings() {
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    const QDir system(QString::fromWCharArray(directory, int(length)));
    QProcess::startDetached(system.filePath(QStringLiteral("mmc.exe")), {system.filePath(QStringLiteral("taskschd.msc"))});
}
} // namespace still
