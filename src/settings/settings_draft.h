#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantList>

namespace still {
class SettingsApply;

QJsonObject normalizeSettings(QJsonObject form);

class SettingsDraft : public QObject {
    Q_OBJECT
public:
    explicit SettingsDraft(SettingsApply &settings, QObject *parent = nullptr);
    const QJsonObject &form() const { return form_; }
    static QVariantList rows(int page);
    QString status() const { return status_; }
    bool statusError() const { return statusError_; }
    void setDraft(const QString &group, const QString &key, const QVariant &value);
    void setSetting(const QString &group, const QString &key, const QVariant &value);
    void restoreDefault(const QString &group, const QString &key);
    void commitDrafts();
    void reloadSaved();
signals:
    void formChanged();
    void statusChanged();
private:
    SettingsApply &settings_;
    QJsonObject form_, submitted_, accepted_;
    QString status_, validationErrorField_;
    bool statusError_ = false;
};
} // namespace still
