#include "platform/startup_task.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

using namespace still;

namespace {
const QString sid = "S-1-5-21-100-200-300-1001";

QString sample() {
    // moc misreads this raw string when the namespace URL uses double quotes.
    return QStringLiteral(R"XML(<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.4" xmlns='http://schemas.microsoft.com/windows/2004/02/mit/task'>
  <RegistrationInfo>
    <Date>2026-11-07T12:00:00</Date><Author>Current user</Author>
    <URI>\Still Startup S-1-5-21-100-200-300-1001</URI>
    <Source>still.startup:S-1-5-21-100-200-300-1001</Source>
    <Documentation>still.startup.task.v1</Documentation>
    <Description>Still login task</Description><Version>1.0</Version>
  </RegistrationInfo>
  <Triggers><LogonTrigger id="Login"><Enabled>true</Enabled><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>S-1-5-21-100-200-300-1001</UserId></LogonTrigger></Triggers>
  <Principals><Principal id="User"><UserId>S-1-5-21-100-200-300-1001</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel><DisplayName>Current user</DisplayName></Principal></Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>false</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <IdleSettings><Duration>PT10M</Duration><WaitTimeout>PT1H</WaitTimeout><StopOnIdleEnd>true</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>
    <AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</Hidden>
    <RunOnlyIfIdle>false</RunOnlyIfIdle><WakeToRun>false</WakeToRun><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority>
    <UseUnifiedSchedulingEngine>false</UseUnifiedSchedulingEngine><DisallowStartOnRemoteAppSession>false</DisallowStartOnRemoteAppSession><Volatile>false</Volatile>
  </Settings>
  <Actions Context="User"><Exec id="Still"><Command>@EXE@</Command></Exec></Actions>
</Task>)XML");
}

QString changed(QString xml, const QString &from, const QString &to) {
    Q_ASSERT(xml.contains(from));
    return xml.replace(from, to);
}
} // namespace

class StartupTaskTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QString executable_, oldExecutable_;
private slots:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        executable_ = directory_.filePath("Still 空间 & current.exe");
        oldExecutable_ = directory_.filePath("Still old.exe");
        for (const auto &path : {executable_, oldExecutable_}) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("fixture"), qint64(7));
        }
    }
    void generatedDefinitionRoundTrips() {
        const auto xml = startup_task::definition(executable_, sid);
        QVERIFY(xml.contains("&amp;"));
        const auto result = startup_task::inspect(xml, executable_, sid, true);
        QVERIFY(result.owned); QCOMPARE(result.status.state, StartupState::Enabled);
        QVERIFY(!result.status.message.isEmpty()); QVERIFY(!result.status.settingsLabel.isEmpty());
    }
    void commandEscapingIsLiteral() {
        const auto path = directory_.filePath("Still & <新版> \"quoted\".exe");
        QXmlStreamReader reader(startup_task::definition(path, sid));
        QString command;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == "Command") command = reader.readElementText();
        }
        QVERIFY(!reader.hasError()); QCOMPARE(command, QDir::toNativeSeparators(path));
    }
    void inspection_data() {
        QTest::addColumn<QString>("xml"); QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("owned"); QTest::addColumn<int>("state");
        const auto add = [](const char *name, const QString &xml, StartupState state = StartupState::NotFound, bool enabled = true, bool owned = true) {
            QTest::newRow(name) << xml << enabled << owned << int(state);
        };
        const auto xml = sample();
        add("schema-with-system-metadata", xml, StartupState::Enabled);
        add("externally-disabled", xml, StartupState::Disabled, false);
        add("xml-disabled", changed(xml, "<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled>", "<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>false</Enabled>"), StartupState::Disabled);
        add("disabled-old-path", changed(xml, "@EXE@", "@OLD@"), StartupState::Disabled, false);
        add("old-executable", changed(xml, "@EXE@", "@OLD@"));
        add("missing-executable", changed(xml, "@EXE@", "@MISSING@"));
        add("foreign-owner", changed(xml, "still.startup:", "another.product:"), StartupState::Unavailable, true, false);
        add("foreign-documentation", changed(xml, "still.startup.task.v1", "another.product"), StartupState::Unavailable, true, false);
        add("another-owner-sid", changed(xml, "<Source>still.startup:" + sid, "<Source>still.startup:S-1-5-21-999"), StartupState::Unavailable, true, false);
        add("wrong-trigger-sid", changed(xml, "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>" + sid, "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>S-1-5-21-999"));
        add("wrong-principal-sid", changed(xml, "<Principal id=\"User\"><UserId>" + sid, "<Principal id=\"User\"><UserId>S-1-5-21-999"));
        add("extra-action", changed(xml, "</Exec></Actions>", "</Exec><Exec><Command>@EXE@</Command></Exec></Actions>"));
        add("shell-action", changed(xml, "<Command>@EXE@</Command>", "<Command>cmd.exe</Command><Arguments>/c @EXE@</Arguments>"));
        add("extra-arguments", changed(xml, "</Command>", "</Command><Arguments>--different-mode</Arguments>"));
        add("extra-trigger", changed(xml, "</LogonTrigger></Triggers>", "</LogonTrigger><BootTrigger><Enabled>true</Enabled></BootTrigger></Triggers>"));
        add("disabled-trigger", changed(xml, "<LogonTrigger id=\"Login\"><Enabled>true</Enabled>", "<LogonTrigger id=\"Login\"><Enabled>false</Enabled>"));
        add("elevated", changed(xml, "LeastPrivilege", "HighestAvailable"));
        add("noninteractive", changed(xml, "InteractiveToken", "S4U"));
        add("action-other-principal", changed(xml, "Context=\"User\"", "Context=\"Other\""));
        add("battery-start-condition", changed(xml, "<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>", "<DisallowStartIfOnBatteries>true</DisallowStartIfOnBatteries>"));
        add("battery-stop-condition", changed(xml, "<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>", "<StopIfGoingOnBatteries>true</StopIfGoingOnBatteries>"));
        add("battery-default-is-restrictive", changed(xml, "<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>", ""));
        add("runtime-limited", changed(xml, "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>", "<ExecutionTimeLimit>PT1H</ExecutionTimeLimit><Priority>"));
        add("runtime-default-is-restrictive", changed(xml, "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>", "<Priority>"));
        add("trigger-runtime-limited", changed(xml, "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>", "<ExecutionTimeLimit>PT1H</ExecutionTimeLimit><UserId>"));
        add("idle-condition", changed(xml, "<RunOnlyIfIdle>false</RunOnlyIfIdle>", "<RunOnlyIfIdle>true</RunOnlyIfIdle>"));
        add("network-condition", changed(xml, "<RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>", "<RunOnlyIfNetworkAvailable>true</RunOnlyIfNetworkAvailable>"));
        add("trigger-expires", changed(xml, "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>", "<EndBoundary>2025-01-01T00:00:00</EndBoundary><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><UserId>"));
        add("privileges", changed(xml, "</RunLevel>", "</RunLevel><RequiredPrivileges><Privilege>SeDebugPrivilege</Privilege></RequiredPrivileges>"));
        add("boolean-schema-forms", changed(changed(xml, "<RunOnlyIfIdle>false</RunOnlyIfIdle>", "<RunOnlyIfIdle>0</RunOnlyIfIdle>"), "<LogonTrigger id=\"Login\"><Enabled>true</Enabled>", "<LogonTrigger id=\"Login\"><Enabled>1</Enabled>"), StartupState::Enabled);
        add("malformed", xml.left(xml.size() - 7), StartupState::Unavailable, true, false);
        add("malformed-trailing", xml + "<Extra/>", StartupState::Unavailable, true, false);
        add("wrong-namespace", changed(xml, "http://schemas.microsoft.com/windows/2004/02/mit/task", "urn:another-task"), StartupState::Unavailable, true, false);
        add("duplicate-ownership-field", changed(xml, "<Documentation>", "<Source>still.startup:" + sid + "</Source><Documentation>"), StartupState::Unavailable, true, false);
    }
    void inspection() {
        QFETCH(QString, xml); QFETCH(bool, enabled); QFETCH(bool, owned); QFETCH(int, state);
        xml.replace("@EXE@", QDir::toNativeSeparators(executable_).toHtmlEscaped());
        xml.replace("@OLD@", QDir::toNativeSeparators(oldExecutable_).toHtmlEscaped());
        xml.replace("@MISSING@", QDir::toNativeSeparators(directory_.filePath("missing.exe")).toHtmlEscaped());
        const auto result = startup_task::inspect(xml, executable_, sid, enabled);
        QCOMPARE(result.owned, owned); QCOMPARE(int(result.status.state), state);
        QVERIFY(!result.status.message.isEmpty());
    }
    void pathAliasResolvesToTheSameFile() {
        QVERIFY(QDir(directory_.path()).mkdir("sub"));
        const auto alias = directory_.path() + "/sub/../" + QFileInfo(executable_).fileName();
        const auto result = startup_task::inspect(startup_task::definition(alias, sid), executable_, sid, true);
        QVERIFY(result.owned); QCOMPARE(result.status.state, StartupState::Enabled);
    }
};

QTEST_GUILESS_MAIN(StartupTaskTest)
#include "startup_task_test.moc"
