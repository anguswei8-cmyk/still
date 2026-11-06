#include "platform/startup_win.h"
#include "platform/startup_task.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <taskschd.h>
#include <QCoreApplication>
#include <QDir>
#include <QScopeGuard>
#include <QTest>
#include <QUuid>

using namespace still;

namespace {
const QString testPrefix = QStringLiteral("Still.StartupTest.5f322622-49ab-4adc-902d-10f6cfc5edf7.");
bool missing(HRESULT result) {
    return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}
BSTR nativeString(const QString &value) {
    return SysAllocStringLen(reinterpret_cast<const OLECHAR *>(value.utf16()), UINT(value.size()));
}
} // namespace

class StartupWindowsTest : public QObject {
    Q_OBJECT
    startup_windows::Identity identity_;
    QString sid_;
    bool initialized_ = false;
    ITaskService *service_ = nullptr;
    ITaskFolder *folder_ = nullptr;

    bool testIdentity() const {
        return !sid_.isEmpty() && identity_.taskName.startsWith(testPrefix + sid_ + QLatin1Char('.'));
    }
    bool change(bool enabled, QString *error) {
        if (!testIdentity()) { *error = "Not a startup test task"; return false; }
        return startup_windows::setEnabled(identity_, enabled, error);
    }
    HRESULT task(IRegisteredTask **value) {
        if (!testIdentity()) return E_ACCESSDENIED;
        const BSTR name = nativeString(identity_.taskName);
        const auto release = qScopeGuard([&] { SysFreeString(name); });
        return name ? folder_->GetTask(name, value) : E_OUTOFMEMORY;
    }
    HRESULT registrationResult() {
        IRegisteredTask *registered = nullptr;
        const auto release = qScopeGuard([&] { if (registered) registered->Release(); });
        return task(&registered);
    }
    QString xml(HRESULT *result) {
        IRegisteredTask *registered = nullptr;
        const auto releaseTask = qScopeGuard([&] { if (registered) registered->Release(); });
        *result = task(&registered);
        if (FAILED(*result)) return {};
        BSTR text = nullptr;
        const auto releaseText = qScopeGuard([&] { SysFreeString(text); });
        *result = registered->get_Xml(&text);
        return SUCCEEDED(*result) ? QString::fromWCharArray(text, int(SysStringLen(text))) : QString();
    }
    HRESULT disableExternally() {
        if (!testIdentity()) return E_ACCESSDENIED;
        IRegisteredTask *registered = nullptr;
        const auto release = qScopeGuard([&] { if (registered) registered->Release(); });
        const HRESULT result = task(&registered);
        return FAILED(result) ? result : registered->put_Enabled(VARIANT_FALSE);
    }
    HRESULT registerFixture(const QString &text) {
        if (!testIdentity()) return E_ACCESSDENIED;
        ITaskDefinition *definition = nullptr;
        const auto releaseDefinition = qScopeGuard([&] { if (definition) definition->Release(); });
        HRESULT result = service_->NewTask(0, &definition);
        if (FAILED(result)) return result;
        const BSTR definitionXml = nativeString(text), name = nativeString(identity_.taskName);
        const auto releaseStrings = qScopeGuard([&] { SysFreeString(definitionXml); SysFreeString(name); });
        if (!definitionXml || !name) return E_OUTOFMEMORY;
        result = definition->put_XmlText(definitionXml);
        if (FAILED(result)) return result;
        VARIANT user{};
        user.vt = VT_BSTR;
        user.bstrVal = nativeString(sid_);
        const auto clear = qScopeGuard([&] { VariantClear(&user); });
        if (!user.bstrVal) return E_OUTOFMEMORY;
        const VARIANT empty{};
        IRegisteredTask *registered = nullptr;
        const auto releaseRegistered = qScopeGuard([&] { if (registered) registered->Release(); });
        return folder_->RegisterTaskDefinition(name, definition, TASK_CREATE_OR_UPDATE, user, empty,
                                               TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    }
private slots:
    void initTestCase() {
        if (qEnvironmentVariable("STILL_WINDOWS_STARTUP_TEST") != "1")
            QSKIP("STILL_WINDOWS_STARTUP_TEST=1 is required to register an isolated current-user test task");
        QString error;
        sid_ = startup_windows::currentUserSid(&error);
        QVERIFY2(!sid_.isEmpty(), qPrintable(error));
        identity_ = {testPrefix + sid_ + QLatin1Char('.') + QUuid::createUuid().toString(QUuid::WithoutBraces),
                     QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
        QVERIFY(testIdentity());
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized_ = SUCCEEDED(result);
        QVERIFY(SUCCEEDED(result) || result == RPC_E_CHANGED_MODE);
        result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                  reinterpret_cast<void **>(&service_));
        QCOMPARE(result, S_OK);
        const VARIANT empty{};
        QCOMPARE(service_->Connect(empty, empty, empty, empty), S_OK);
        const BSTR root = SysAllocString(L"\\");
        const auto release = qScopeGuard([&] { SysFreeString(root); });
        QVERIFY(root);
        QCOMPARE(service_->GetFolder(root, &folder_), S_OK);
        QVERIFY(missing(registrationResult()));
    }
    void queryDoesNotRegister() {
        QVERIFY(missing(registrationResult()));
        for (int i = 0; i < 3; ++i) {
            const auto current = startup_windows::status(identity_);
            QCOMPARE(current.state, StartupState::Disabled);
            QVERIFY(!current.message.isEmpty());
            QVERIFY(!current.settingsLabel.isEmpty());
        }
        QVERIFY(missing(registrationResult()));
    }
    void registerAndRemove() {
        QString error;
        QVERIFY2(change(true, &error), qPrintable(error));
        QCOMPARE(startup_windows::status(identity_).state, StartupState::Enabled);
        QVERIFY2(change(true, &error), qPrintable(error));
        QVERIFY2(change(false, &error), qPrintable(error));
        QCOMPARE(startup_windows::status(identity_).state, StartupState::Disabled);
        QVERIFY(missing(registrationResult()));
        QVERIFY2(change(false, &error), qPrintable(error));
    }
    void respectsExternalDisable() {
        QString error;
        QVERIFY2(change(true, &error), qPrintable(error));
        QCOMPARE(disableExternally(), S_OK);
        for (int i = 0; i < 3; ++i) QCOMPARE(startup_windows::status(identity_).state, StartupState::Disabled);
        QVERIFY2(change(true, &error), qPrintable(error));
        QCOMPARE(startup_windows::status(identity_).state, StartupState::Enabled);
    }
    void oldPathCanBeReplacedOrRemoved() {
        const auto oldPath = QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath()).filePath("Still-startup-old.exe"));
        QCOMPARE(registerFixture(startup_task::definition(oldPath, sid_)), S_OK);
        QCOMPARE(startup_windows::status(identity_).state, StartupState::NotFound);
        QString error;
        QVERIFY2(change(true, &error), qPrintable(error));
        QCOMPARE(startup_windows::status(identity_).state, StartupState::Enabled);
        QCOMPARE(registerFixture(startup_task::definition(oldPath, sid_)), S_OK);
        QCOMPARE(startup_windows::status(identity_).state, StartupState::NotFound);
        QVERIFY2(change(false, &error), qPrintable(error));
        QVERIFY(missing(registrationResult()));
    }
    void doesNotOverwriteOrRemoveForeignRegistration() {
        auto foreign = startup_task::definition(identity_.executablePath, sid_);
        QVERIFY(foreign.contains(QStringLiteral("still.startup.task.v1")));
        foreign.replace(QStringLiteral("still.startup.task.v1"), QStringLiteral("foreign.startup.test.fixture"));
        QCOMPARE(registerFixture(foreign), S_OK);
        HRESULT result = S_OK;
        const auto before = xml(&result);
        QCOMPARE(result, S_OK);
        QVERIFY(!before.isEmpty());
        QCOMPARE(startup_windows::status(identity_).state, StartupState::Unavailable);
        QString error;
        QVERIFY(!change(true, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!change(false, &error));
        QCOMPARE(xml(&result), before);
        QCOMPARE(result, S_OK);
    }
    void cleanup() {
        if (!folder_) return;
        QVERIFY(testIdentity());
        const BSTR name = nativeString(identity_.taskName);
        const auto release = qScopeGuard([&] { SysFreeString(name); });
        QVERIFY(name);
        const HRESULT result = folder_->DeleteTask(name, 0);
        QVERIFY(result == S_OK || missing(result));
    }
    void cleanupTestCase() {
        if (folder_) { folder_->Release(); folder_ = nullptr; }
        if (service_) { service_->Release(); service_ = nullptr; }
        if (initialized_) CoUninitialize();
    }
};

QTEST_GUILESS_MAIN(StartupWindowsTest)
#include "startup_windows_test.moc"
