#include "platform/api_credentials.h"
#include <QCoreApplication>
#include <QScopeGuard>
#include <QTest>
#include <QUuid>

using namespace still;

class ApiCredentialsTest : public QObject {
    Q_OBJECT
private slots:
    void rejectsInvalidSizeWithoutAccessingTheStore() {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        const QString account = "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString error;
        QVERIFY(!writeApiCredential(account, {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!writeApiCredential(account, QByteArray(ApiCredentialMaxBytes + 1, 'x'), &error));
        QVERIFY(!error.isEmpty());
#else
        QSKIP("No native credential implementation on this platform");
#endif
    }
    void roundTripInAnIsolatedAccount() {
        if (qEnvironmentVariable("STILL_API_CREDENTIAL_TEST") != "1") QSKIP("Explicit gate required for temporary native credential writes");
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        const QString account = "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString secondAccount = "test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto cleanup = qScopeGuard([&] {
            QString error;
            QVERIFY2(removeApiCredential(account, &error), qPrintable(error));
        });
        auto value = readApiCredential(account);
        QVERIFY2(value.error.isEmpty(), qPrintable(value.error));
        QVERIFY(value.key.isEmpty());
        QString error;
        QVERIFY2(removeApiCredential(account, &error), qPrintable(error));
        const QByteArray first("still-test-\0secret", 18);
        QVERIFY2(writeApiCredential(account, first, &error), qPrintable(error));
        value = readApiCredential(account);
        QVERIFY2(value.error.isEmpty(), qPrintable(value.error));
        QVERIFY(value.key == first);
        const auto other = readApiCredential(secondAccount);
        QVERIFY2(other.error.isEmpty(), qPrintable(other.error));
        QVERIFY(other.key.isEmpty());
        const QByteArray replacement(ApiCredentialMaxBytes, 't');
        QVERIFY2(writeApiCredential(account, replacement, &error), qPrintable(error));
        QVERIFY(readApiCredential(account).key == replacement);
        QVERIFY(!writeApiCredential(account, {}, &error));
        QVERIFY(readApiCredential(account).key == replacement);
        QVERIFY(!writeApiCredential(account, QByteArray(ApiCredentialMaxBytes + 1, 'x'), &error));
        QVERIFY(readApiCredential(account).key == replacement);
        QVERIFY2(removeApiCredential(account, &error), qPrintable(error));
        value = readApiCredential(account);
        QVERIFY2(value.error.isEmpty(), qPrintable(value.error));
        QVERIFY(value.key.isEmpty());
        QVERIFY2(removeApiCredential(account, &error), qPrintable(error));
#else
        QSKIP("No native credential implementation on this platform");
#endif
    }
};

QTEST_GUILESS_MAIN(ApiCredentialsTest)
#include "api_credentials_test.moc"
