#include "startup_task.h"
#include "ui/i18n.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QScopeGuard>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <sddl.h>
#endif

namespace still::startup_task {
namespace {
const QString taskNamespace = "http://schemas.microsoft.com/windows/2004/02/mit/task";
const QString documentation = "still.startup.task.v1";
using Values = QHash<QString, QString>;

bool nextElement(QXmlStreamReader &reader) {
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            reader.raiseError("Task XML must not declare entities");
        if (token == QXmlStreamReader::StartElement) {
            if (reader.namespaceUri() != taskNamespace) reader.raiseError("Unexpected task namespace");
            return !reader.hasError();
        }
        if (token == QXmlStreamReader::EndElement) return false;
        if (token == QXmlStreamReader::Characters && !reader.isWhitespace()) reader.raiseError("Unexpected task text");
    }
    return false;
}

bool attributesMatch(const QXmlStreamReader &reader, const QStringList &allowed = {}) {
    for (const auto &attribute : reader.attributes())
        if (!attribute.namespaceUri().isEmpty() || !allowed.contains(attribute.name().toString())) return false;
    return true;
}

Values readValues(QXmlStreamReader &reader, bool &matches) {
    Values values;
    while (nextElement(reader)) {
        const auto name = reader.name().toString();
        if (values.contains(name)) reader.raiseError("Duplicate task property");
        matches &= attributesMatch(reader);
        if (name == "RequiredPrivileges" || name == "Repetition") {
            matches = false; reader.skipCurrentElement(); values.insert(name, {});
        } else {
            values.insert(name, reader.readElementText());
        }
    }
    return values;
}

bool only(const Values &values, const QStringList &allowed) {
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}

bool boolean(const Values &values, const QString &key, bool defaultValue, bool expected) {
    if (!values.contains(key)) return defaultValue == expected;
    const auto value = values.value(key).trimmed();
    return expected ? value == "true" || value == "1" : value == "false" || value == "0";
}

bool zeroDuration(const QString &value) {
    return value == "PT0S" || value == "PT0M" || value == "PT0H" || value == "P0D";
}

bool sameUser(const QString &user, const QString &sid) {
    if (user == sid) return true;
#ifdef Q_OS_WIN
    // Task Scheduler can serialize a logon trigger's SID as DOMAIN\\user.
    BYTE resolved[SECURITY_MAX_SID_SIZE];
    wchar_t domain[256];
    DWORD sidSize = sizeof(resolved), domainSize = sizeof(domain) / sizeof(domain[0]);
    SID_NAME_USE use;
    if (!LookupAccountNameW(nullptr, reinterpret_cast<LPCWSTR>(user.utf16()), resolved, &sidSize, domain, &domainSize, &use)) return false;
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(resolved, &text)) return false;
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text) == sid;
#else
    return false;
#endif
}

bool sameExecutable(const QString &command, const QString &executablePath) {
    const QFileInfo registered(command), current(executablePath);
    if (!registered.isAbsolute() || !registered.isFile() || !current.isFile()) return false;
    const auto path = registered.canonicalFilePath(), expected = current.canonicalFilePath();
#ifdef Q_OS_WIN
    return !path.isEmpty() && path.compare(expected, Qt::CaseInsensitive) == 0;
#else
    return !path.isEmpty() && path == expected;
#endif
}

StartupStatus status(StartupState state, const char *message) {
    return {state, t(message), t("任务计划程序")};
}
} // namespace

QString definition(const QString &executablePath, const QString &sid) {
    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement("Task"); writer.writeDefaultNamespace(taskNamespace); writer.writeAttribute("version", "1.2");
    writer.writeStartElement("RegistrationInfo");
    writer.writeTextElement("Source", "still.startup:" + sid);
    writer.writeTextElement("Documentation", documentation);
    writer.writeTextElement("Description", "Start Still when the current user signs in.");
    writer.writeEndElement();
    writer.writeStartElement("Triggers"); writer.writeStartElement("LogonTrigger");
    writer.writeTextElement("Enabled", "true");
    writer.writeTextElement("ExecutionTimeLimit", "PT0S");
    writer.writeTextElement("UserId", sid);
    writer.writeEndElement(); writer.writeEndElement();
    writer.writeStartElement("Principals"); writer.writeStartElement("Principal"); writer.writeAttribute("id", "StillUser");
    writer.writeTextElement("UserId", sid);
    writer.writeTextElement("LogonType", "InteractiveToken");
    writer.writeTextElement("RunLevel", "LeastPrivilege");
    writer.writeEndElement(); writer.writeEndElement();
    writer.writeStartElement("Settings");
    writer.writeTextElement("MultipleInstancesPolicy", "IgnoreNew");
    writer.writeTextElement("DisallowStartIfOnBatteries", "false");
    writer.writeTextElement("StopIfGoingOnBatteries", "false");
    writer.writeTextElement("RunOnlyIfNetworkAvailable", "false");
    writer.writeTextElement("RunOnlyIfIdle", "false");
    writer.writeTextElement("Enabled", "true");
    writer.writeTextElement("ExecutionTimeLimit", "PT0S");
    writer.writeEndElement();
    writer.writeStartElement("Actions"); writer.writeAttribute("Context", "StillUser"); writer.writeStartElement("Exec");
    writer.writeTextElement("Command", QDir::toNativeSeparators(executablePath));
    writer.writeEndElement(); writer.writeEndElement();
    writer.writeEndElement(); writer.writeEndDocument();
    return xml;
}

Inspection inspect(const QString &xml, const QString &executablePath, const QString &sid, bool enabled) {
    QXmlStreamReader reader(xml);
    bool matches = true;
    Values registration, trigger, principal, action, settings;
    QString principalId, context;
    int triggerCount = 0, principalCount = 0, actionCount = 0;
    QSet<QString> sections;
    const auto invalid = [] { return Inspection{false, status(StartupState::Unavailable, "无法确认此任务属于 Still；未修改任务计划程序中的登记。")}; };
    if (!nextElement(reader) || reader.name() != "Task") return invalid();
    matches &= attributesMatch(reader, {"version"});
    const auto version = reader.attributes().value("version");
    matches &= version == "1.2" || version == "1.3" || version == "1.4";
    while (nextElement(reader)) {
        const auto section = reader.name().toString();
        if (sections.contains(section)) reader.raiseError("Duplicate task section");
        sections.insert(section);
        matches &= attributesMatch(reader, section == "Actions" ? QStringList{"Context"} : QStringList{});
        if (section == "RegistrationInfo") {
            registration = readValues(reader, matches);
            matches &= only(registration, {"Source", "Documentation", "Description", "URI", "Author", "Date", "Version", "SecurityDescriptor"});
        } else if (section == "Triggers") {
            while (nextElement(reader)) {
                ++triggerCount;
                matches &= attributesMatch(reader, {"id"});
                if (reader.name() != "LogonTrigger") { matches = false; reader.skipCurrentElement(); continue; }
                trigger = readValues(reader, matches);
            }
        } else if (section == "Principals") {
            while (nextElement(reader)) {
                ++principalCount;
                matches &= attributesMatch(reader, {"id"});
                if (reader.name() != "Principal") { matches = false; reader.skipCurrentElement(); continue; }
                principalId = reader.attributes().value("id").toString();
                principal = readValues(reader, matches);
            }
        } else if (section == "Actions") {
            context = reader.attributes().value("Context").toString();
            while (nextElement(reader)) {
                ++actionCount;
                matches &= attributesMatch(reader, {"id"});
                if (reader.name() != "Exec") { matches = false; reader.skipCurrentElement(); continue; }
                action = readValues(reader, matches);
            }
        } else if (section == "Settings") {
            QSet<QString> seen;
            while (nextElement(reader)) {
                const auto name = reader.name().toString();
                if (seen.contains(name)) reader.raiseError("Duplicate task setting");
                seen.insert(name);
                matches &= attributesMatch(reader);
                if (name == "IdleSettings" || name == "NetworkSettings") {
                    const auto values = readValues(reader, matches);
                    matches &= only(values, name == "IdleSettings" ? QStringList{"Duration", "WaitTimeout", "StopOnIdleEnd", "RestartOnIdle"} : QStringList{"Name", "Id"});
                } else if (name == "RestartOnFailure" || name == "MaintenanceSettings") {
                    matches = false; reader.skipCurrentElement();
                } else {
                    settings.insert(name, reader.readElementText());
                }
            }
        } else if (section == "Data") {
            reader.skipCurrentElement();
        } else {
            matches = false; reader.skipCurrentElement();
        }
    }
    nextElement(reader); // Read the document end so trailing malformed XML cannot establish ownership.
    if (reader.hasError() || registration.value("Source") != "still.startup:" + sid || registration.value("Documentation") != documentation) return invalid();
    if (!enabled || boolean(settings, "Enabled", true, false))
        return {true, status(StartupState::Disabled, "已在任务计划程序中关闭登录时启动。")};

    matches &= triggerCount == 1 && principalCount == 1 && actionCount == 1;
    matches &= only(trigger, {"Enabled", "UserId", "Delay", "ExecutionTimeLimit"});
    matches &= boolean(trigger, "Enabled", true, true) && sameUser(trigger.value("UserId"), sid);
    matches &= zeroDuration(trigger.value("Delay", "PT0M"));
    matches &= zeroDuration(trigger.value("ExecutionTimeLimit", "PT72H"));
    matches &= only(principal, {"UserId", "LogonType", "RunLevel", "DisplayName"});
    matches &= sameUser(principal.value("UserId"), sid) && principal.value("LogonType") == "InteractiveToken";
    matches &= principal.value("RunLevel", "LeastPrivilege") == "LeastPrivilege" && !principalId.isEmpty() && context == principalId;
    matches &= only(action, {"Command", "Arguments", "WorkingDirectory"}) && action.value("Arguments").isEmpty() && action.value("WorkingDirectory").isEmpty();
    matches &= sameExecutable(action.value("Command"), executablePath);
    matches &= only(settings, {"MultipleInstancesPolicy", "DisallowStartIfOnBatteries", "StopIfGoingOnBatteries", "RunOnlyIfNetworkAvailable", "RunOnlyIfIdle", "Enabled", "ExecutionTimeLimit", "AllowStartOnDemand", "AllowHardTerminate", "StartWhenAvailable", "WakeToRun", "Hidden", "DeleteExpiredTaskAfter", "Priority", "UseUnifiedSchedulingEngine", "DisallowStartOnRemoteAppSession", "Volatile", "NetworkProfileName"});
    matches &= boolean(settings, "Enabled", true, true);
    // These defaults are restrictive in Task Scheduler, so omission must not count as false or unlimited.
    matches &= boolean(settings, "DisallowStartIfOnBatteries", true, false) && boolean(settings, "StopIfGoingOnBatteries", true, false);
    matches &= zeroDuration(settings.value("ExecutionTimeLimit", "PT72H"));
    for (const auto &name : {"RunOnlyIfNetworkAvailable", "RunOnlyIfIdle", "StartWhenAvailable", "WakeToRun", "Hidden", "UseUnifiedSchedulingEngine", "DisallowStartOnRemoteAppSession", "Volatile"})
        matches &= boolean(settings, name, false, false);
    matches &= boolean(settings, "AllowStartOnDemand", true, true) && boolean(settings, "AllowHardTerminate", true, true);
    matches &= settings.value("MultipleInstancesPolicy", "IgnoreNew") == "IgnoreNew" && settings.value("Priority", "7") == "7";
    matches &= zeroDuration(settings.value("DeleteExpiredTaskAfter", "PT0S"));
    if (!matches) return {true, status(StartupState::NotFound, "任务计划程序中的启动路径或条件已变更；重新开启可修复当前版本的登记。")};
    return {true, status(StartupState::Enabled, "已在任务计划程序中开启登录时启动。")};
}
} // namespace still::startup_task
