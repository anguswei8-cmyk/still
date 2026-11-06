#include "api_profile.h"
#include "imaging/api_proofreader.h"
#include "storage/store.h"
#include "ui/i18n.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

namespace still {
namespace {
QString profileError(const ApiProfile &profile) {
    const auto error = ApiProofreader::endpointError(profile.endpoint);
    if (!error.isEmpty()) return error;
    if (profile.model.size() > 1024) return t("模型名称不能超过 1024 个字符。");
    return profile.model.trimmed().isEmpty() ? t("请输入模型名称。") : QString();
}
} // namespace

QString apiProfilePath() { return QDir(dataDirectory()).filePath("api-profile.json"); }

std::optional<ApiProfile> loadApiProfile(QString *error) {
    if (error) error->clear();
    QFile file(apiProfilePath());
    if (!file.exists()) return std::nullopt;
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return std::nullopt; }
    const auto bytes = file.read(16385);
    const auto document = QJsonDocument::fromJson(bytes);
    const auto object = document.object();
    if (bytes.size() > 16384 || !document.isObject() || object.size() != 2
        || !object.value("endpoint").isString() || !object.value("model").isString()) {
        if (error) *error = t("API 设置文件格式不正确，请重新保存服务地址和模型。");
        return std::nullopt;
    }
    ApiProfile profile{QUrl(object.value("endpoint").toString(), QUrl::StrictMode), object.value("model").toString()};
    const auto validation = profileError(profile);
    if (!validation.isEmpty()) { if (error) *error = validation; return std::nullopt; }
    return profile;
}

bool saveApiProfile(const ApiProfile &profile, QString *error) {
    if (error) error->clear();
    const auto validation = profileError(profile);
    if (!validation.isEmpty()) { if (error) *error = validation; return false; }
    return writeJson(apiProfilePath(), {{"endpoint", profile.endpoint.toString(QUrl::FullyEncoded)}, {"model", profile.model}}, error);
}

QString apiCredentialAccount(const QUrl &endpoint) {
    return QString::fromLatin1(QCryptographicHash::hash(endpoint.toEncoded(QUrl::FullyEncoded), QCryptographicHash::Sha256).toHex());
}

} // namespace still
