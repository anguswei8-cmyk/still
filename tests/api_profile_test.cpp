#include "settings/api_profile.h"
#include "storage/store.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace still;

class ApiProfileTest : public QObject {
    Q_OBJECT
private:
    QByteArray previousDataDir_;
    bool hadDataDir_ = false;
    std::unique_ptr<QTemporaryDir> directory_;
private slots:
    void init() {
        hadDataDir_ = qEnvironmentVariableIsSet("STILL_DATA_DIR"); previousDataDir_ = qgetenv("STILL_DATA_DIR");
        directory_ = std::make_unique<QTemporaryDir>(); QVERIFY(directory_->isValid()); qputenv("STILL_DATA_DIR", directory_->path().toUtf8());
    }
    void cleanup() {
        if (hadDataDir_) qputenv("STILL_DATA_DIR", previousDataDir_); else qunsetenv("STILL_DATA_DIR");
        directory_.reset();
    }
    void absentProfileIsUnconfiguredWithoutCreatingFiles() {
        QString error = "old error"; QVERIFY(!loadApiProfile(&error)); QVERIFY(error.isEmpty());
        QVERIFY(!QFile::exists(apiProfilePath()));
    }
    void profileRoundTripsOnlyAddressAndModel() {
        const ApiProfile profile{QUrl("https://example.com/v1/chat/completions"), QStringLiteral("校对模型")};
        QString error; QVERIFY2(saveApiProfile(profile, &error), qPrintable(error));
        const auto loaded = loadApiProfile(&error); QVERIFY2(loaded, qPrintable(error));
        QCOMPARE(loaded->endpoint, profile.endpoint); QCOMPARE(loaded->model, profile.model);
        QFile file(apiProfilePath()); QVERIFY(file.open(QIODevice::ReadOnly)); const auto object = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(object.keys(), QStringList({"endpoint", "model"}));
        QVERIFY(apiProfilePath() != settingsPath());
    }
    void invalidDocumentsAreRejected_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("broken") << QByteArray("{"); QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("missing") << QByteArray("{\"endpoint\":\"https://example.com/v1/chat/completions\"}");
        QTest::newRow("model-type") << QByteArray("{\"endpoint\":\"https://example.com/v1/chat/completions\",\"model\":42}");
        QTest::newRow("secret-field") << QByteArray("{\"endpoint\":\"https://example.com/v1/chat/completions\",\"model\":\"model\",\"apiKey\":\"secret\"}");
        QTest::newRow("unsafe-address") << QByteArray("{\"endpoint\":\"http://example.com/v1/chat/completions\",\"model\":\"model\"}");
        QTest::newRow("empty-model") << QByteArray("{\"endpoint\":\"https://example.com/v1/chat/completions\",\"model\":\" \"}");
        QTest::newRow("oversized") << QByteArray(16385, ' ');
    }
    void invalidDocumentsAreRejected() {
        QFETCH(QByteArray, bytes); QFile file(apiProfilePath()); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QString error; QVERIFY(!loadApiProfile(&error)); QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
    void failedSaveRetainsThePreviousProfile() {
        const ApiProfile previous{QUrl("https://example.com/v1/chat/completions"), "first"}; QVERIFY(saveApiProfile(previous));
        QString error; QVERIFY(!saveApiProfile({QUrl("http://untrusted.example/v1/chat/completions"), "second"}, &error)); QVERIFY(!error.isEmpty());
        const auto loaded = loadApiProfile(); QVERIFY(loaded); QCOMPARE(loaded->model, previous.model); QCOMPARE(loaded->endpoint, previous.endpoint);
    }
    void credentialsAreBoundToTheWholeEndpoint() {
        const QUrl address("https://example.com/v1/chat/completions"); const auto account = apiCredentialAccount(address);
        QCOMPARE(account.size(), 64); QCOMPARE(account, apiCredentialAccount(address));
        QVERIFY(account != apiCredentialAccount(QUrl("https://other.example/v1/chat/completions")));
        QVERIFY(account != apiCredentialAccount(QUrl("https://example.com/other/chat/completions")));
        QVERIFY(account != apiCredentialAccount(QUrl("https://example.com:8443/v1/chat/completions")));
        QVERIFY(!account.contains("example"));
    }
};

QTEST_GUILESS_MAIN(ApiProfileTest)
#include "api_profile_test.moc"
