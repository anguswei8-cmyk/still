#include "api_credentials.h"
#include "ui/i18n.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#include <QScopeGuard>

namespace still {
namespace {
QString target(const QString &account) { return QStringLiteral("local.still.api-key/") + account; }
QString credentialError(DWORD code) { return t("Windows 凭据管理器操作失败（错误 %1）").arg(code); }
} // namespace

ApiCredential readApiCredential(const QString &account) {
    const QString name = target(account);
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<LPCWSTR>(name.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD code = GetLastError();
        return code == ERROR_NOT_FOUND ? ApiCredential{} : ApiCredential{{}, credentialError(code)};
    }
    const auto release = qScopeGuard([&] { CredFree(credential); });
    return {QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob), qsizetype(credential->CredentialBlobSize)), {}};
}

bool writeApiCredential(const QString &account, const QByteArray &key, QString *error) {
    error->clear();
    // MinGW still defines the legacy 512-byte limit; supported Windows allows 5 * 512 bytes.
    static_assert(ApiCredentialMaxBytes == 5 * 512);
    if (key.isEmpty() || key.size() > ApiCredentialMaxBytes) {
        *error = t("API 密钥必须为 1 至 2560 字节");
        return false;
    }
    QString name = target(account);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = reinterpret_cast<LPWSTR>(name.data());
    credential.CredentialBlobSize = DWORD(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(key.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (CredWriteW(&credential, 0)) return true;
    *error = credentialError(GetLastError());
    return false;
}

bool removeApiCredential(const QString &account, QString *error) {
    error->clear();
    const QString name = target(account);
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(name.utf16()), CRED_TYPE_GENERIC, 0)) return true;
    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) return true;
    *error = credentialError(code);
    return false;
}
} // namespace still
