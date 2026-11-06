#include "api_credentials.h"
#include "ui/i18n.h"

namespace still {
ApiCredential readApiCredential(const QString &) { return {{}, t("当前平台尚未支持系统 API 密钥存储")}; }
bool writeApiCredential(const QString &, const QByteArray &, QString *error) { *error = readApiCredential({}).error; return false; }
bool removeApiCredential(const QString &, QString *error) { *error = readApiCredential({}).error; return false; }
} // namespace still
