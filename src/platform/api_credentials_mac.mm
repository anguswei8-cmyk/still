#include "api_credentials.h"
#include "ui/i18n.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <QMutex>

namespace still {
namespace {
QMutex keychainMutex;

// shortcut: ad-hoc builds use the file-based keychain; use data protection once provisioned signing is available.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
struct KeychainAccess {
    QMutexLocker<QMutex> lock{&keychainMutex};
    SecKeychainRef keychain = nullptr;
    Boolean interaction = false;
    bool restore = false;
    OSStatus status;

    KeychainAccess() : status(SecKeychainGetUserInteractionAllowed(&interaction)) {
        if (status != errSecSuccess) return;
        status = SecKeychainSetUserInteractionAllowed(false);
        restore = status == errSecSuccess;
        if (status == errSecSuccess) status = SecKeychainCopyDefault(&keychain);
    }
    ~KeychainAccess() {
        if (keychain) CFRelease(keychain);
        if (restore) SecKeychainSetUserInteractionAllowed(interaction);
    }
};
#pragma clang diagnostic pop

NSMutableDictionary *attributes(const QString &account) {
    return [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
              (__bridge id)kSecAttrService: @"local.still.api-key",
              (__bridge id)kSecAttrAccount: account.toNSString(),
              (__bridge id)kSecAttrSynchronizable: @NO,
              (__bridge id)kSecUseDataProtectionKeychain: @NO} mutableCopy];
}

NSMutableDictionary *query(const QString &account, SecKeychainRef keychain) {
    NSMutableDictionary *result = attributes(account);
    result[(__bridge id)kSecMatchSearchList] = @[(__bridge id)keychain];
    return result;
}

QString keychainError(OSStatus status) {
    if (status == errSecInteractionNotAllowed || status == errSecAuthFailed)
        return t("钥匙串已锁定或需要系统授权，请解锁或在钥匙串访问中处理后重试");
    return t("系统钥匙串操作失败（错误 %1）").arg(status);
}
} // namespace

ApiCredential readApiCredential(const QString &account) {
    @autoreleasepool {
        KeychainAccess access;
        if (access.status != errSecSuccess) return {{}, keychainError(access.status)};
        NSMutableDictionary *request = query(account, access.keychain);
        request[(__bridge id)kSecReturnData] = @YES;
        request[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        CFTypeRef value = nullptr;
        const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)request, &value);
        if (status == errSecItemNotFound) return {};
        if (status != errSecSuccess) return {{}, keychainError(status)};
        NSData *data = CFBridgingRelease(value);
        return {QByteArray(static_cast<const char *>(data.bytes), qsizetype(data.length)), {}};
    }
}

bool writeApiCredential(const QString &account, const QByteArray &key, QString *error) {
    error->clear();
    if (key.isEmpty() || key.size() > ApiCredentialMaxBytes) {
        *error = t("API 密钥必须为 1 至 2560 字节");
        return false;
    }
    @autoreleasepool {
        KeychainAccess access;
        if (access.status != errSecSuccess) { *error = keychainError(access.status); return false; }
        NSData *data = [NSData dataWithBytes:key.constData() length:NSUInteger(key.size())];
        NSDictionary *changes = @{(__bridge id)kSecValueData: data};
        OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query(account, access.keychain), (__bridge CFDictionaryRef)changes);
        if (status == errSecItemNotFound) {
            NSMutableDictionary *item = attributes(account);
            item[(__bridge id)kSecUseKeychain] = (__bridge id)access.keychain;
            item[(__bridge id)kSecValueData] = data;
            item[(__bridge id)kSecAttrLabel] = @"Still API Key";
            status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
        }
        if (status != errSecSuccess) *error = keychainError(status);
        return status == errSecSuccess;
    }
}

bool removeApiCredential(const QString &account, QString *error) {
    error->clear();
    @autoreleasepool {
        KeychainAccess access;
        if (access.status != errSecSuccess) { *error = keychainError(access.status); return false; }
        const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query(account, access.keychain));
        if (status == errSecSuccess || status == errSecItemNotFound) return true;
        *error = keychainError(status);
        return false;
    }
}
} // namespace still
