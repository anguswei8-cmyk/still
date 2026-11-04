#include "i18n.h"
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPalette>
#include <QStyleHints>

namespace pixclone {
InterfacePreferences::Catalog::Catalog() {
    QFile file(":/brand/en.json");
    if (file.open(QIODevice::ReadOnly)) entries = QJsonDocument::fromJson(file.readAll()).object();
}
QString InterfacePreferences::Catalog::translate(const char *context, const char *source, const char *, int) const {
    return qstrcmp(context, "Still") == 0 ? entries.value(QString::fromUtf8(source)).toString() : QString();
}
InterfacePreferences &InterfacePreferences::instance() { static InterfacePreferences value; return value; }
InterfacePreferences::InterfacePreferences() {
#ifdef Q_OS_WIN
    // Segoe UI handles Latin text; use the UI face for CJK instead of the serif fallback.
    QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, QStringLiteral("Microsoft YaHei UI"));
#endif
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (theme_ == "system") { updatePalette(); emit changed(); }
    });
}
bool InterfacePreferences::dark() const {
    return theme_ == "dark" || (theme_ == "system" && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
}
void InterfacePreferences::apply(const QJsonObject &options) {
    const QString language = options.value("language").toString("zh_CN");
    const QString theme = options.value("theme").toString("system");
    if (initialized_ && language == language_ && theme == theme_) return;
    initialized_ = true;
    const bool languageChanged = language_ != language;
    language_ = language == "en" ? "en" : "zh_CN";
    theme_ = QStringList{"light", "dark", "system"}.contains(theme) ? theme : "system";
    if (languageChanged) {
        if (english()) QCoreApplication::installTranslator(&catalog_);
        else QCoreApplication::removeTranslator(&catalog_);
    }
    QGuiApplication::setApplicationDisplayName(english() ? "Still" : t("拾影"));
    updatePalette();
    emit changed();
}
QString InterfacePreferences::translatedCaption(const QString &caption) const {
    if (english()) return catalog_.entries.value(caption).toString(caption);
    for (auto it = catalog_.entries.begin(); it != catalog_.entries.end(); ++it) if (it.value().toString() == caption) return it.key();
    return caption;
}
void InterfacePreferences::updatePalette() {
    const bool night = dark();
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(night ? "#181818" : "#f4f5f7"));
    palette.setColor(QPalette::WindowText, QColor(night ? "#ededed" : "#242731"));
    palette.setColor(QPalette::Base, QColor(night ? "#1c1c1c" : "#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor(night ? "#242424" : "#e2e6ed"));
    palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
    palette.setColor(QPalette::Button, palette.color(QPalette::Base));
    palette.setColor(QPalette::ButtonText, palette.color(QPalette::Text));
    palette.setColor(QPalette::Highlight, QColor(night ? "#555555" : "#454b57"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor(night ? "#adadad" : "#626875"));
    palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Base));
    palette.setColor(QPalette::ToolTipText, palette.color(QPalette::Text));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(night ? "#898989" : "#858e9e"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, palette.color(QPalette::Disabled, QPalette::Text));
    QGuiApplication::setPalette(palette);
}
} // namespace pixclone
