#include "settings/api_settings_dialog.h"
#include "settings/api_profile.h"
#include "storage/store.h"
#include "ui/i18n.h"
#include "api_credentials_fixture.h"
#include "api_test_server.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace still;

static QLineEdit *field(ApiSettingsDialog &dialog, const char *name) { return dialog.findChild<QLineEdit *>(name); }
static QPushButton *button(ApiSettingsDialog &dialog, const char *name) { return dialog.findChild<QPushButton *>(name); }
static QString status(ApiSettingsDialog &dialog) { return dialog.findChild<QLabel *>("ApiSettingsStatus")->text(); }

class ApiSettingsDialogTest : public QObject {
    Q_OBJECT
private:
    QByteArray previousDataDir_;
    bool hadDataDir_ = false;
    std::unique_ptr<QTemporaryDir> directory_;
private slots:
    void init() {
        credential_fixture::reset();
        hadDataDir_ = qEnvironmentVariableIsSet("STILL_DATA_DIR"); previousDataDir_ = qgetenv("STILL_DATA_DIR");
        directory_ = std::make_unique<QTemporaryDir>(); QVERIFY(directory_->isValid()); qputenv("STILL_DATA_DIR", directory_->path().toUtf8());
        InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", "light"}});
    }
    void cleanup() {
        if (hadDataDir_) qputenv("STILL_DATA_DIR", previousDataDir_); else qunsetenv("STILL_DATA_DIR");
        credential_fixture::reset(); directory_.reset();
    }
    void defaultsAreLocalAndOpeningNeverCreatesAProfile() {
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        QCOMPARE(dialog.windowModality(), Qt::WindowModal);
        QVERIFY(field(dialog, "ApiEndpoint")->text().isEmpty()); QVERIFY(field(dialog, "ApiModel")->text().isEmpty()); QVERIFY(field(dialog, "ApiKey")->text().isEmpty());
        QCOMPARE(field(dialog, "ApiKey")->echoMode(), QLineEdit::Password);
        QVERIFY(!QFile::exists(apiProfilePath())); QVERIFY(credential_fixture::keys.isEmpty());
        button(dialog, "ApiSaveSettings")->click(); QVERIFY(!status(dialog).isEmpty()); QVERIFY(!QFile::exists(apiProfilePath()));
    }
    void savingSeparatesSecretFromNonsecretProfile() {
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiEndpoint")->setText("https://example.com/v1/chat/completions"); field(dialog, "ApiModel")->setText("model"); field(dialog, "ApiKey")->setText("private-api-key");
        QSignalSpy accepted(&dialog, &QDialog::accepted); button(dialog, "ApiSaveSettings")->click(); QCOMPARE(accepted.size(), 1);
        const auto profile = loadApiProfile(); QVERIFY(profile); QCOMPARE(profile->model, QString("model"));
        QCOMPARE(credential_fixture::keys.value(apiCredentialAccount(profile->endpoint)), QByteArray("private-api-key"));
        QFile file(apiProfilePath()); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(!file.readAll().contains("private-api-key"));
        QVERIFY(field(dialog, "ApiKey")->text().isEmpty());
    }
    void emptyInputRetainsOnlyThisAddressesSavedKey() {
        const ApiProfile initial{QUrl("https://example.com/v1/chat/completions"), "model"}; QVERIFY(saveApiProfile(initial));
        credential_fixture::keys[apiCredentialAccount(initial.endpoint)] = "first-key";
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false); QVERIFY(field(dialog, "ApiKey")->text().isEmpty());
        field(dialog, "ApiModel")->setText("new-model"); button(dialog, "ApiSaveSettings")->click(); QCOMPARE(loadApiProfile()->model, QString("new-model"));
        QCOMPARE(credential_fixture::keys.value(apiCredentialAccount(initial.endpoint)), QByteArray("first-key"));
        field(dialog, "ApiKey")->setText("typed-old-provider-key"); field(dialog, "ApiEndpoint")->setText("https://other.example/v1/chat/completions");
        QVERIFY(field(dialog, "ApiKey")->text().isEmpty()); button(dialog, "ApiSaveSettings")->click();
        QCOMPARE(loadApiProfile()->endpoint, initial.endpoint); QCOMPARE(credential_fixture::keys.size(), 1); QVERIFY(status(dialog).contains("API Key"));
    }
    void deletingAKeyDoesNotSaveOtherInputsOrOtherCredentials() {
        const ApiProfile initial{QUrl("https://example.com/v1/chat/completions"), "model"}; QVERIFY(saveApiProfile(initial));
        const auto account = apiCredentialAccount(initial.endpoint); credential_fixture::keys[account] = "delete-me"; credential_fixture::keys["other-account"] = "keep-me";
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiModel")->setText("uncommitted-model"); QVERIFY(button(dialog, "ApiRemoveKey")->isEnabled()); button(dialog, "ApiRemoveKey")->click();
        QVERIFY(!credential_fixture::keys.contains(account)); QCOMPARE(credential_fixture::keys.value("other-account"), QByteArray("keep-me"));
        QCOMPARE(loadApiProfile()->model, initial.model); QVERIFY(!button(dialog, "ApiRemoveKey")->isEnabled());
    }
    void credentialFailureStopsSaveAndDoesNotRevealTheTypedKey() {
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiEndpoint")->setText("https://example.com/v1/chat/completions"); field(dialog, "ApiModel")->setText("model"); field(dialog, "ApiKey")->setText("secret-key");
        credential_fixture::error = QStringLiteral("系统凭据存储不可用"); QSignalSpy accepted(&dialog, &QDialog::accepted);
        button(dialog, "ApiSaveSettings")->click(); QCOMPARE(accepted.size(), 0); QVERIFY(!QFile::exists(apiProfilePath())); QVERIFY(credential_fixture::keys.isEmpty());
        QVERIFY(status(dialog).contains(credential_fixture::error)); QVERIFY(!status(dialog).contains("secret-key"));
    }
    void profileWriteFailureReportsTheAlreadySavedKey() {
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiEndpoint")->setText("https://example.com/v1/chat/completions"); field(dialog, "ApiModel")->setText("model"); field(dialog, "ApiKey")->setText("secret-key");
        QVERIFY(QDir().mkdir(apiProfilePath())); QSignalSpy accepted(&dialog, &QDialog::accepted);
        button(dialog, "ApiSaveSettings")->click(); QCOMPARE(accepted.size(), 0);
        QCOMPARE(credential_fixture::keys.value(apiCredentialAccount(QUrl("https://example.com/v1/chat/completions"))), QByteArray("secret-key"));
        QVERIFY(status(dialog).contains(QStringLiteral("密钥已保存"))); QVERIFY(!status(dialog).contains("secret-key"));
        QVERIFY(!loadApiProfile());
    }
    void connectionTestSendsOnlyTheFixedSampleWithoutSaving() {
        ApiTestServer server; QVERIFY(server.server.isListening());
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiEndpoint")->setText(server.endpoint().toString()); field(dialog, "ApiModel")->setText("test-model"); field(dialog, "ApiKey")->setText("test-secret");
        QTest::qWait(10); QVERIFY(server.requests.isEmpty()); button(dialog, "ApiTestConnection")->click(); QVERIFY(!field(dialog, "ApiEndpoint")->isEnabled());
        QTRY_VERIFY(status(dialog).contains(QStringLiteral("连接测试成功")));
        QCOMPARE(server.requests.size(), 1);
        const auto request = QJsonDocument::fromJson(server.requestBody).object();
        QCOMPARE(request.value("model").toString(), QString("test-model"));
        QCOMPARE(request.value("messages").toArray().last().toObject().value("content").toString(), QString("Still API connection check."));
        QVERIFY(server.requestHeaders.contains("Bearer test-secret")); QVERIFY(status(dialog).contains("20 tokens"));
        QVERIFY(!QFile::exists(apiProfilePath())); QVERIFY(credential_fixture::keys.isEmpty()); QVERIFY(!status(dialog).contains("test-secret"));
        server.body = R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"OK"}}],"usage":{"prompt_tokens":5,"completion_tokens":2}})";
        button(dialog, "ApiTestConnection")->click(); QTRY_VERIFY(status(dialog).contains(QStringLiteral("合计 未知")));
        QVERIFY(status(dialog).contains(QStringLiteral("输入 5，输出 2")));
    }
    void cancelAndCloseLeaveTheProfileUntouched() {
        ApiTestServer server; server.hold = true; QVERIFY(server.server.isListening());
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiEndpoint")->setText(server.endpoint().toString()); field(dialog, "ApiModel")->setText("test-model");
        button(dialog, "ApiTestConnection")->click(); QTRY_VERIFY(!server.requests.isEmpty()); button(dialog, "ApiTestConnection")->click();
        QTRY_VERIFY(field(dialog, "ApiEndpoint")->isEnabled()); QVERIFY(status(dialog).contains(QStringLiteral("已取消")));
        button(dialog, "ApiTestConnection")->click(); dialog.reject(); QVERIFY(!QFile::exists(apiProfilePath())); QVERIFY(credential_fixture::keys.isEmpty());
    }
    void languageAndThemeChangesPreserveUncommittedInputs() {
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        field(dialog, "ApiEndpoint")->setText("http://127.0.0.1:4567/v1/chat/completions"); field(dialog, "ApiModel")->setText("draft-model"); field(dialog, "ApiKey")->setText("draft-key");
        InterfacePreferences::instance().apply({{"language", "en"}, {"theme", "dark"}});
        QCOMPARE(field(dialog, "ApiModel")->text(), QString("draft-model")); QCOMPARE(field(dialog, "ApiKey")->text(), QString("draft-key"));
        QVERIFY(!field(dialog, "ApiEndpoint")->accessibleName().isEmpty()); QVERIFY(!button(dialog, "ApiTestConnection")->autoDefault());
        QVERIFY(!QFile::exists(apiProfilePath()));
    }
    void visualStates_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme"); QTest::addColumn<QSize>("size");
        QTest::newRow("normal-light-zh") << QString("zh_CN") << QString("light") << QSize(620, 400);
        QTest::newRow("normal-dark-en") << QString("en") << QString("dark") << QSize(620, 400);
        QTest::newRow("compact-light-en") << QString("en") << QString("light") << QSize(480, 420);
        QTest::newRow("compact-dark-zh") << QString("zh_CN") << QString("dark") << QSize(480, 420);
    }
    void visualStates() {
        if (QGuiApplication::platformName() != "offscreen") QSKIP("Visual matrix runs only offscreen.");
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        ApiSettingsDialog dialog; dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.resize(size); dialog.show();
        const QString folder = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../output/visual/offscreen"); QVERIFY(QDir().mkpath(folder));
        const auto capture = [&](const QString &state) {
            QApplication::processEvents();
            return dialog.grab().save(folder + "/api-settings-" + state + '-' + QTest::currentDataTag() + ".png");
        };
        QVERIFY(capture("empty")); QCOMPARE(dialog.width(), size.width());
        ApiTestServer server; QVERIFY(server.server.isListening());
        field(dialog, "ApiEndpoint")->setText(server.endpoint().toString()); field(dialog, "ApiModel")->setText("ocr-proofreader");
        button(dialog, "ApiTestConnection")->click(); QTRY_VERIFY(status(dialog).contains("20 tokens")); QVERIFY(capture("connected"));
        server.status = 401; button(dialog, "ApiTestConnection")->click(); QTRY_VERIFY(status(dialog).contains("HTTP 401")); QVERIFY(capture("error"));
        QCOMPARE(dialog.width(), size.width()); QVERIFY(dialog.minimumSizeHint().width() <= size.width());
        for (auto *control : {button(dialog, "ApiRemoveKey"), button(dialog, "ApiTestConnection"), button(dialog, "ApiSaveSettings"), button(dialog, "ApiCloseSettings")}) {
            QVERIFY(control->geometry().right() < dialog.width()); QVERIFY(control->geometry().left() >= 0);
            QVERIFY(!control->accessibleName().isEmpty());
        }
    }
};

QTEST_MAIN(ApiSettingsDialogTest)
#include "api_settings_dialog_test.moc"
