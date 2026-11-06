#include "platform/startup.h"
#include <QCoreApplication>
#include <QOperatingSystemVersion>
#include <QScopeGuard>
#include <QTest>
#import <Foundation/Foundation.h>

using namespace still;

class StartupNativeTest : public QObject {
    Q_OBJECT
    bool attempted_ = false;
private slots:
    void readingDoesNotRegister() {
        if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) QSKIP("SMAppService needs macOS 13 or later");
        const auto before = startupStatus();
        QVERIFY(before.state != StartupState::Unavailable);
        QVERIFY(!before.message.isEmpty());
        for (int i = 0; i < 3; ++i) QCOMPARE(startupStatus().state, before.state);
    }
    void registerAndRemoveTheTestBundle() {
        if (qEnvironmentVariable("STILL_STARTUP_REGISTRATION_TEST") != "1") QSKIP("Explicit gate required for temporary system registration of the test bundle");
        QCOMPARE(QString::fromUtf8(NSBundle.mainBundle.bundleIdentifier.UTF8String), QString("local.still.startup-regression"));
        const auto initial = startupStatus().state;
        QVERIFY(initial == StartupState::Disabled || initial == StartupState::NotFound);
        attempted_ = true;
        const auto cleanup = qScopeGuard([] {
            QString error;
            if (!setStartupEnabled(false, &error)) qWarning().noquote() << error;
        });
        QString error;
        QVERIFY2(setStartupEnabled(true, &error), qPrintable(error));
        const auto registered = startupStatus().state;
        QVERIFY(registered == StartupState::Enabled || registered == StartupState::RequiresApproval);
        QVERIFY2(setStartupEnabled(false, &error), qPrintable(error));
        QTRY_COMPARE(startupStatus().state, StartupState::Disabled);
        QVERIFY2(setStartupEnabled(false, &error), qPrintable(error));
    }
    void cleanupTestCase() {
        if (!attempted_) return;
        QString error;
        QVERIFY2(setStartupEnabled(false, &error), qPrintable(error));
        QTRY_COMPARE(startupStatus().state, StartupState::Disabled);
    }
};

QTEST_GUILESS_MAIN(StartupNativeTest)
#include "startup_native_test.moc"
