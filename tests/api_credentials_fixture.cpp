#include "api_credentials_fixture.h"
#include "platform/api_credentials.h"

namespace still::credential_fixture {
QMap<QString, QByteArray> keys;
QString error;
void reset() { keys.clear(); error.clear(); }
} // namespace still::credential_fixture

namespace still {
ApiCredential readApiCredential(const QString &account) {
    return credential_fixture::error.isEmpty() ? ApiCredential{credential_fixture::keys.value(account), {}} : ApiCredential{{}, credential_fixture::error};
}
bool writeApiCredential(const QString &account, const QByteArray &key, QString *error) {
    if (!credential_fixture::error.isEmpty()) { if (error) *error = credential_fixture::error; return false; }
    credential_fixture::keys[account] = key; if (error) error->clear(); return true;
}
bool removeApiCredential(const QString &account, QString *error) {
    if (!credential_fixture::error.isEmpty()) { if (error) *error = credential_fixture::error; return false; }
    credential_fixture::keys.remove(account); if (error) error->clear(); return true;
}
} // namespace still
