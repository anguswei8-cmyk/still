#pragma once

#include <QByteArray>
#include <QString>

namespace still {
inline constexpr int ApiCredentialMaxBytes = 2560;
struct ApiCredential {
    QByteArray key;
    QString error;
};

// Account is the normalized API endpoint's SHA-256; credentials stay in the OS store.
ApiCredential readApiCredential(const QString &account);
bool writeApiCredential(const QString &account, const QByteArray &key, QString *error);
bool removeApiCredential(const QString &account, QString *error);
} // namespace still
