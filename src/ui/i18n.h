#pragma once

#include <QCoreApplication>
#include <QJsonObject>
#include <QObject>
#include <QTranslator>

namespace still {
inline QString t(const char *source) { return QCoreApplication::translate("Still", source); }
inline QString t(const QString &source) { return t(source.toUtf8().constData()); }

// One immutable catalog also serves worker-thread error messages and QML bindings.
class InterfacePreferences final : public QObject {
    Q_OBJECT
public:
    static InterfacePreferences &instance();
    void apply(const QJsonObject &options);
    bool dark() const;
    bool english() const { return language_ == "en"; }
    QString translatedCaption(const QString &caption) const;
signals:
    void changed();
private:
    InterfacePreferences();
    void updatePalette();
    class Catalog final : public QTranslator {
    public:
        Catalog();
        bool isEmpty() const override { return false; }
        QString translate(const char *context, const char *source, const char *, int) const override;
        QJsonObject entries;
    } catalog_;
    QString language_ = "zh_CN", theme_ = "system";
    bool initialized_ = false;
};
} // namespace still
