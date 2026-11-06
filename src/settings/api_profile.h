#pragma once

#include <QUrl>
#include <optional>

namespace still {

struct ApiProfile {
    QUrl endpoint;
    QString model;
};

QString apiProfilePath();
std::optional<ApiProfile> loadApiProfile(QString *error = nullptr);
bool saveApiProfile(const ApiProfile &profile, QString *error = nullptr);
QString apiCredentialAccount(const QUrl &endpoint);

} // namespace still
