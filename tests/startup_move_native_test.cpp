#include "platform/startup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <cstdio>
#include <memory>
#import <Foundation/Foundation.h>

using namespace still;

namespace {
const QString identifierPrefix = QStringLiteral("local.still.startup-move-validation.");
const QString executableName = QStringLiteral("StartupMoveProbe");

QString stateName(StartupState state) {
    switch (state) {
    case StartupState::Unavailable: return "Unavailable";
    case StartupState::Disabled: return "Disabled";
    case StartupState::Enabled: return "Enabled";
    case StartupState::RequiresApproval: return "RequiresApproval";
    case StartupState::NotFound: return "NotFound";
    }
    return {};
}

QString executableIn(const QString &bundle) { return bundle + "/Contents/MacOS/" + executableName; }

int probeMain(const QStringList &arguments) {
    QJsonObject result{{"ok", false}, {"state", "Unavailable"}};
    const auto refuse = [&](const QString &reason) {
        result["error"] = reason; result["refused"] = true;
        const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        return 2;
    };
    if (qEnvironmentVariable("STILL_STARTUP_MOVE_TEST") != "1") return refuse("Explicit movement-test gate required");
    if (arguments.size() != 5 || !QStringList{"status", "enable", "disable"}.contains(arguments[2])) return refuse("Invalid probe arguments");
    const QString root = arguments[3], identifier = arguments[4], nonce = identifier.mid(identifierPrefix.size());
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString rootPattern = "^still-startup-move-" + QRegularExpression::escape(nonce) + "-[A-Za-z0-9]{6}$";
    const QString actualIdentifier = QString::fromUtf8(NSBundle.mainBundle.bundleIdentifier.UTF8String);
    const QString actualBundle = QFileInfo(QString::fromUtf8(NSBundle.mainBundle.bundlePath.UTF8String)).canonicalFilePath();
    if (!identifier.startsWith(identifierPrefix) || QUuid(nonce).isNull() || QUuid(nonce).toString(QUuid::WithoutBraces) != nonce
        || root != canonicalRoot || QFileInfo(root).dir().canonicalPath() != QFileInfo(QDir::tempPath()).canonicalFilePath()
        || !QRegularExpression(rootPattern).match(QFileInfo(root).fileName()).hasMatch()
        || actualIdentifier != identifier || !QStringList{root + "/Original.app", root + "/Moved.app"}.contains(actualBundle)
        || QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath() != executableIn(actualBundle))
        return refuse("Probe is not the isolated temporary test bundle");

    QString error;
    bool ok = true;
    if (arguments[2] != "status") ok = setStartupEnabled(arguments[2] == "enable", &error);
    const auto status = startupStatus();
    result["ok"] = ok; result["state"] = stateName(status.state); result["error"] = error;
    result["bundleID"] = actualIdentifier; result["bundlePath"] = actualBundle;
    const auto bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    return 0;
}
} // namespace

class StartupMoveNativeTest : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory_;
    QString root_, identifier_, original_, moved_, error_;
    bool attempted_ = false, childStopped_ = true, timedOut_ = false;

    QJsonObject run(const QString &program, const QStringList &arguments, const QProcessEnvironment &environment, int *exitCode = nullptr) {
        error_.clear(); QProcess process; process.setProcessEnvironment(environment); process.start(program, arguments);
        if (!process.waitForStarted(5000)) {
            error_ = process.errorString();
            if (process.state() != QProcess::NotRunning) { timedOut_ = true; process.kill(); childStopped_ = process.waitForFinished(5000); }
            return {};
        }
        if (!process.waitForFinished(15000)) {
            timedOut_ = true; process.kill(); childStopped_ = process.waitForFinished(5000);
            error_ = "Child process timed out; fixture will be retained"; return {};
        }
        if (exitCode) *exitCode = process.exitCode();
        if (process.exitStatus() != QProcess::NormalExit) { error_ = "Child process crashed"; return {}; }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            error_ = "Invalid child reply: " + process.readAllStandardError(); return {};
        }
        return document.object();
    }

    QJsonObject probe(const QString &bundle, const QString &action) {
        int exitCode = -1;
        const auto reply = run(executableIn(bundle), {"--startup-move-probe", action, root_, identifier_}, QProcessEnvironment::systemEnvironment(), &exitCode);
        if (exitCode != 0 && error_.isEmpty()) error_ = "Probe failed: " + reply.value("error").toString();
        return error_.isEmpty() ? reply : QJsonObject();
    }

    bool createBundle() {
        const QString contents = original_ + "/Contents";
        if (!QDir().mkpath(contents + "/MacOS")) { error_ = "Could not create the isolated app directory"; return false; }
        QFile executable(QCoreApplication::applicationFilePath());
        if (!executable.copy(executableIn(original_))) { error_ = executable.errorString(); return false; }
        NSDictionary *info = @{@"CFBundleExecutable": executableName.toNSString(), @"CFBundleIdentifier": identifier_.toNSString(),
            @"CFBundleName": @"Still Startup Move Validation", @"CFBundlePackageType": @"APPL", @"LSUIElement": @YES};
        NSError *failure = nil;
        if (![info writeToURL:[NSURL fileURLWithPath:(contents + "/Info.plist").toNSString()] error:&failure]) {
            error_ = QString::fromUtf8(failure.localizedDescription.UTF8String); return false;
        }
        for (const auto &arguments : {QStringList{"--force", "--sign", "-", original_}, QStringList{"--verify", "--strict", original_}}) {
            QProcess signer; signer.start("/usr/bin/codesign", arguments);
            if (!signer.waitForStarted(5000)) {
                error_ = signer.errorString();
                if (signer.state() != QProcess::NotRunning) {
                    timedOut_ = true; signer.kill(); childStopped_ = signer.waitForFinished(5000); directory_->setAutoRemove(false);
                }
                return false;
            }
            if (!signer.waitForFinished(15000)) {
                signer.kill(); childStopped_ = signer.waitForFinished(5000); timedOut_ = true;
                directory_->setAutoRemove(false); error_ = "Signing process timed out; fixture will be retained"; return false;
            }
            if (signer.exitStatus() != QProcess::NormalExit || signer.exitCode() != 0) {
                error_ = "Signing failed: " + signer.readAllStandardError(); return false;
            }
        }
        return true;
    }

    bool disabledAt(const QString &bundle) {
        const auto removal = probe(bundle, "disable");
        if (removal.isEmpty()) return false;
        const auto status = probe(bundle, "status");
        return status.value("state") == "Disabled";
    }

private slots:
    void helperRejectsMissingGateAndTheParentProcess() {
        const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces), identifier = identifierPrefix + nonce;
        QTemporaryDir root(QDir::tempPath() + "/still-startup-move-" + nonce + "-XXXXXX"); QVERIFY(root.isValid());
        const QStringList arguments{"--startup-move-probe", "enable", QFileInfo(root.path()).canonicalFilePath(), identifier};
        auto environment = QProcessEnvironment::systemEnvironment(); environment.remove("STILL_STARTUP_MOVE_TEST"); int exitCode = -1;
        auto reply = run(QCoreApplication::applicationFilePath(), arguments, environment, &exitCode);
        QVERIFY2(error_.isEmpty(), qPrintable(error_)); QCOMPARE(exitCode, 2); QVERIFY(reply.value("refused").toBool());
        QCOMPARE(reply.value("error").toString(), QString("Explicit movement-test gate required"));
        environment.insert("STILL_STARTUP_MOVE_TEST", "1");
        reply = run(QCoreApplication::applicationFilePath(), arguments, environment, &exitCode);
        QVERIFY2(error_.isEmpty(), qPrintable(error_)); QCOMPARE(exitCode, 2); QVERIFY(reply.value("refused").toBool());
        QCOMPARE(reply.value("error").toString(), QString("Probe is not the isolated temporary test bundle"));
    }

    void movedBundleCanBeDisabledAndRegisteredAgain() {
        if (qEnvironmentVariable("STILL_STARTUP_MOVE_TEST") != "1") QSKIP("Explicit gate required for temporary native registration and bundle movement");
        if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13)) QSKIP("SMAppService needs macOS 13 or later");
        QVERIFY2(childStopped_ && !timedOut_, "An earlier child timeout prevents native registration");
        const QString nonce = QUuid::createUuid().toString(QUuid::WithoutBraces); identifier_ = identifierPrefix + nonce;
        directory_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/still-startup-move-" + nonce + "-XXXXXX"); QVERIFY(directory_->isValid());
        root_ = QFileInfo(directory_->path()).canonicalFilePath(); original_ = root_ + "/Original.app"; moved_ = root_ + "/Moved.app";
        qInfo().noquote() << "STARTUP_MOVE_FIXTURE" << root_ << identifier_;
        QVERIFY2(createBundle(), qPrintable(error_));
        auto reply = probe(original_, "status"); QVERIFY2(!reply.isEmpty(), qPrintable(error_));
        QVERIFY(reply.value("state") == "Disabled" || reply.value("state") == "NotFound");
        directory_->setAutoRemove(false); attempted_ = true;
        reply = probe(original_, "enable"); QVERIFY2(!reply.isEmpty(), qPrintable(error_)); QVERIFY2(reply.value("ok").toBool(), qPrintable(reply.value("error").toString()));
        QVERIFY(reply.value("state") == "Enabled" || reply.value("state") == "RequiresApproval");
        qInfo().noquote() << "STARTUP_MOVE_ORIGINAL_STATE" << reply.value("state").toString();
        QVERIFY(QDir().rename(original_, moved_)); QVERIFY(!QFileInfo::exists(original_)); QVERIFY(QFileInfo::exists(moved_));
        // A filesystem rename is exercised here; Finder/LaunchServices relocation and actual login remain separate acceptance gates.
        reply = probe(moved_, "status"); QVERIFY2(!reply.isEmpty(), qPrintable(error_));
        QCOMPARE(reply.value("bundleID").toString(), identifier_); QCOMPARE(reply.value("bundlePath").toString(), moved_);
        QVERIFY(reply.value("state") != "Unavailable");
        qInfo().noquote() << "STARTUP_MOVE_RELOCATED_STATE" << reply.value("state").toString();
        reply = probe(moved_, "disable"); QVERIFY2(!reply.isEmpty(), qPrintable(error_)); QVERIFY2(reply.value("ok").toBool(), qPrintable(reply.value("error").toString()));
        QCOMPARE(probe(moved_, "status").value("state").toString(), QString("Disabled"));
        reply = probe(moved_, "enable"); QVERIFY2(!reply.isEmpty(), qPrintable(error_)); QVERIFY2(reply.value("ok").toBool(), qPrintable(reply.value("error").toString()));
        QVERIFY(reply.value("state") == "Enabled" || reply.value("state") == "RequiresApproval");
        qInfo().noquote() << "STARTUP_MOVE_REREGISTERED_STATE" << reply.value("state").toString();
        if (reply.value("state") == "RequiresApproval") {
            reply = probe(moved_, "enable"); QVERIFY2(!reply.isEmpty(), qPrintable(error_)); QVERIFY(!reply.value("ok").toBool());
            QVERIFY(!reply.value("error").toString().isEmpty()); QCOMPARE(reply.value("state").toString(), QString("RequiresApproval"));
        }
        QVERIFY2(disabledAt(moved_), qPrintable(error_));
    }

    void cleanupTestCase() {
        if (!attempted_) return;
        if (!childStopped_) { qWarning().noquote() << "STARTUP_MOVE_FIXTURE_RETAINED" << root_ << identifier_; QFAIL("Child termination was not confirmed; fixture retained for cleanup"); }
        bool disabled = disabledAt(QFileInfo::exists(moved_) ? moved_ : original_);
        if (!disabled && QFileInfo::exists(moved_) && !QFileInfo::exists(original_) && QDir().rename(moved_, original_)) disabled = disabledAt(original_);
        if (!disabled || timedOut_) {
            qWarning().noquote() << "STARTUP_MOVE_FIXTURE_RETAINED" << root_ << identifier_ << error_;
            QFAIL("Cleanup did not prove a safe removal; isolated bundle retained for inspection");
        }
        directory_->setAutoRemove(true);
        QVERIFY2(directory_->remove(), qPrintable(directory_->errorString()));
    }
};

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().value(1) == "--startup-move-probe") return probeMain(application.arguments());
    StartupMoveNativeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "startup_move_native_test.moc"
