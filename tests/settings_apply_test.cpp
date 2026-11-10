#include "settings/settings_apply.h"
#include "storage/history.h"
#include "platform/platform.h"
#include "storage/store.h"
#include "clipboard/clipboard_archive.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace still;
namespace still { int nativeIdForTest(const QKeySequence &sequence); }

static QJsonObject preferences(const QString &directory, const QString &key = "Ctrl+1") {
    return {{"hotkeys", QJsonObject{{"capture", key}}}, {"history", QJsonObject{{"dir", directory}}}};
}

class SettingsApplyTest : public QObject {
    Q_OBJECT
private slots:
    void defaultHotkeysMatchThePlatformDefaults() {
        const auto defaults = defaultHotkeys();
        QCOMPARE(defaults.size(), hotkeyNames.size());
        for (const auto &name : hotkeyNames) QVERIFY(!defaults.value(name).toString().isEmpty());
        QCOMPARE(defaults.value("capture").toString(), QString("Ctrl+1"));
        QCOMPARE(defaults.value("pasteAsPin").toString(), QString("Ctrl+Shift+V"));
#ifdef Q_OS_WIN
        QCOMPARE(defaults.value("clipboardHistory").toString(), QString("Ctrl+Alt+Space"));
#else
        QCOMPARE(defaults.value("clipboardHistory").toString(), QString("Ctrl+Shift+Space"));
#endif
    }
    void backupCommitIsAsynchronousAndPreservesUnspecifiedSettings() {
        QTemporaryDir directory;
        const QString file = directory.filePath("settings.json");
        auto initial = preferences(directory.filePath("history"));
        initial["future"] = QJsonObject{{"keep", 42}};
        QVERIFY(writeJson(file, initial));
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys, competitor;
        SettingsApply apply(initial, file, keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished), activated(&keys, &Hotkeys::activated);
        const int previousId = nativeIdForTest(QKeySequence("Ctrl+1"));
        QCOMPARE(readJson(file), initial);
        QString error;
        QVERIFY2(apply.applyBackup({{"hotkeys", QJsonObject{{"capture", "Ctrl+3"}}}, {"ocr", QJsonObject{{"language", "en-US"}}}}, &error), qPrintable(error));
        QVERIFY(apply.busy()); QVERIFY(finished.isEmpty()); QCOMPARE(apply.settings(), initial);
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1"));
        const int reservedId = nativeIdForTest(QKeySequence("Ctrl+3")); QVERIFY(reservedId);
        QVERIFY(keys.dispatchNative(previousId)); QVERIFY(!keys.dispatchNative(reservedId)); QCOMPARE(activated.size(), 1);
        QTRY_VERIFY(!apply.busy()); QCOMPARE(finished.size(), 1); QVERIFY2(finished.first()[0].toBool(), qPrintable(finished.first()[3].toString()));
        QVERIFY(finished.first()[2].toStringList().isEmpty());
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+3"));
        QVERIFY(!keys.dispatchNative(previousId)); QVERIFY(keys.dispatchNative(reservedId));
        QCOMPARE(apply.settings().value("future"), initial.value("future"));
        QCOMPARE(apply.settings().value("ocr").toObject().value("language").toString(), QString("en-US"));
        QCOMPARE(readJson(file), apply.settings()); QVERIFY(competitor.bind(0, QKeySequence("Ctrl+1")));
    }
    void backupConflictRejectsTheWholeChange_data() {
        QTest::addColumn<QString>("captureKey"); QTest::addColumn<QString>("quickKey"); QTest::addColumn<bool>("externalClaim");
        QTest::newRow("external-conflict") << QString("Ctrl+3") << QString("Ctrl+9") << true;
        QTest::newRow("duplicate-imported-keys") << QString("Ctrl+3") << QString("Ctrl+3") << false;
        QTest::newRow("self-occupied-swap") << QString("Ctrl+2") << QString("Ctrl+1") << false;
        QTest::newRow("unsupported-key") << QString("Ctrl+3") << QString("Ctrl+F12") << false;
    }
    void backupConflictRejectsTheWholeChange() {
        QFETCH(QString, captureKey); QFETCH(QString, quickKey); QFETCH(bool, externalClaim);
        QTemporaryDir directory; const QString file = directory.filePath("settings.json");
        auto initial = preferences(directory.filePath("history"));
        initial["hotkeys"] = QJsonObject{{"capture", "Ctrl+1"}, {"quickCapture", "Ctrl+2"}};
        QVERIFY(writeJson(file, initial)); QFile stored(file); QVERIFY(stored.open(QIODevice::ReadOnly));
        const auto bytes = stored.readAll(); stored.close();
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys, competitor;
        SettingsApply apply(initial, file, keys, history);
        if (externalClaim) QVERIFY(competitor.bind(0, QKeySequence("Ctrl+9")));
        const int captureId = nativeIdForTest(QKeySequence("Ctrl+1")), quickId = nativeIdForTest(QKeySequence("Ctrl+2"));
        QSignalSpy finished(&apply, &SettingsApply::finished), activated(&keys, &Hotkeys::activated);
        QString error;
        QVERIFY(!apply.applyBackup({{"hotkeys", QJsonObject{{"capture", captureKey}, {"quickCapture", quickKey}}},
            {"history", QJsonObject{{"dir", directory.filePath("new-history")}}}, {"ocr", QJsonObject{{"language", "en-US"}}}}, &error));
        QVERIFY(error.contains(hotkeyLabels[1])); QVERIFY(finished.isEmpty()); QVERIFY(!apply.busy());
        QCOMPARE(apply.settings(), initial); QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1")); QCOMPARE(keys.sequence(1), QKeySequence("Ctrl+2"));
        QVERIFY(keys.dispatchNative(captureId)); QVERIFY(keys.dispatchNative(quickId)); QCOMPARE(activated.size(), 2);
        QVERIFY(competitor.bind(1, QKeySequence("Ctrl+3"))); QVERIFY(!QFile::exists(directory.filePath("new-history")));
        QVERIFY(stored.open(QIODevice::ReadOnly)); QCOMPARE(stored.readAll(), bytes);
    }
    void backupWhileSavingDoesNotInterfereWithPendingChanges() {
        QTemporaryDir directory; const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys, competitor;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        apply.apply({{"capture", QJsonObject{{"showMagnifier", false}}}});
        apply.apply({{"ocr", QJsonObject{{"language", "en-US"}}}});
        QString error;
        QVERIFY(!apply.applyBackup({{"hotkeys", QJsonObject{{"capture", "Ctrl+3"}}}, {"appearance", QJsonObject{{"theme", "dark"}}}}, &error));
        QVERIFY(!error.isEmpty()); QVERIFY(finished.isEmpty()); QVERIFY(apply.busy());
        QVERIFY(competitor.bind(0, QKeySequence("Ctrl+3")));
        QTRY_VERIFY(!apply.busy()); QCOMPARE(finished.size(), 2);
        QCOMPARE(apply.settings().value("capture").toObject().value("showMagnifier"), QJsonValue(false));
        QCOMPARE(apply.settings().value("ocr").toObject().value("language").toString(), QString("en-US"));
        QVERIFY(!apply.settings().contains("appearance")); QCOMPARE(apply.settings().value("hotkeys"), initial.value("hotkeys"));
        QCOMPARE(readJson(directory.filePath("settings.json")), apply.settings());
    }
    void backupWhileHistoryIsConfiguringDoesNotEmitAResult() {
        QTemporaryDir directory; const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys, competitor;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        history.reconfigure(initial.value("history").toObject(), [](QString &) { return true; });
        QVERIFY(history.isConfiguring()); QString error;
        QVERIFY(!apply.applyBackup({{"hotkeys", QJsonObject{{"capture", "Ctrl+3"}}}}, &error));
        QVERIFY(!error.isEmpty()); QVERIFY(finished.isEmpty()); QVERIFY(!apply.busy());
        QVERIFY(competitor.bind(0, QKeySequence("Ctrl+3"))); QTRY_VERIFY(!history.isConfiguring());
        QVERIFY(finished.isEmpty()); QCOMPARE(apply.settings(), initial);
    }
    void backupMigrationFailureKeepsTheOriginalFileAndBothStores_data() {
        QTest::addColumn<bool>("blockHistory"); QTest::newRow("history-destination") << true; QTest::newRow("clipboard-destination") << false;
    }
    void backupMigrationFailureKeepsTheOriginalFileAndBothStores() {
        QFETCH(bool, blockHistory);
        QTemporaryDir directory; const QString file = directory.filePath("settings.json");
        const QString oldHistory = directory.filePath("history"), oldClips = directory.filePath("clips");
        const QString nextHistory = directory.filePath("new-history"), nextClips = directory.filePath("new-clips");
        auto initial = preferences(oldHistory); initial["clipboard"] = QJsonObject{{"dir", oldClips}}; QVERIFY(writeJson(file, initial));
        QFile stored(file); QVERIFY(stored.open(QIODevice::ReadOnly)); const auto bytes = stored.readAll(); stored.close();
        ClipboardArchive archive(initial.value("clipboard").toObject()); QMimeData mime; mime.setText("Retain clipboard"); archive.add(captureClipboard(mime));
        QTRY_COMPARE(archive.entries().size(), 1); const auto clip = archive.entries().first();
        HistoryStore history(initial.value("history").toObject()); QImage picture(30, 30, QImage::Format_ARGB32); picture.fill(Qt::white); history.remember(picture);
        QTRY_COMPARE(historyFiles(oldHistory).size(), 1); const QString oldImage = historyFiles(oldHistory).first();
        const QString blocked = blockHistory ? nextHistory : nextClips; QVERIFY(QDir().mkpath(blocked));
        QFile unrelated(QDir(blocked).filePath("unrelated.txt")); QVERIFY(unrelated.open(QIODevice::WriteOnly)); QCOMPARE(unrelated.write("keep"), 4); unrelated.close();
        Hotkeys keys, competitor; SettingsApply apply(initial, file, keys, history, nullptr, &archive); QSignalSpy finished(&apply, &SettingsApply::finished);
        QString error; QVERIFY2(apply.applyBackup({{"history", QJsonObject{{"dir", nextHistory}}}, {"clipboard", QJsonObject{{"dir", nextClips}}},
            {"hotkeys", QJsonObject{{"capture", "Ctrl+3"}}}}, &error), qPrintable(error));
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.first()[0].toBool()); QVERIFY(!finished.first()[3].toString().isEmpty());
        QCOMPARE(apply.settings(), initial); QCOMPARE(history.directory(), oldHistory); QCOMPARE(archive.directory(), oldClips);
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1")); QVERIFY(!competitor.bind(0, QKeySequence("Ctrl+1"))); QVERIFY(competitor.bind(1, QKeySequence("Ctrl+3")));
        QCOMPARE(QImage(oldImage).convertToFormat(QImage::Format_ARGB32), picture); QVERIFY(QFile::exists(QDir(oldClips).filePath(clip.id + ".clip")));
        QVERIFY(stored.open(QIODevice::ReadOnly)); QCOMPARE(stored.readAll(), bytes);
        QVERIFY(unrelated.open(QIODevice::ReadOnly)); QCOMPARE(unrelated.readAll(), QByteArray("keep"));
    }
    void rapidChangesKeepLatestForm() {
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        auto candidate = initial;
        candidate["capture"] = QJsonObject{{"showMagnifier", false}};
        apply.apply(candidate);
        candidate["clipboard"] = QJsonObject{{"enabled", false}};
        apply.apply(candidate);
        candidate["clipboard"] = QJsonObject{{"enabled", true}};
        apply.apply(candidate);
        QTRY_VERIFY(finished.count() == 2 && !apply.busy());
        QCOMPARE(apply.settings(), candidate);
        QCOMPARE(readJson(directory.filePath("settings.json")), candidate);
        QVERIFY(finished.first()[0].toBool());
        QVERIFY(finished.last()[0].toBool());
    }
    void pendingSettingsKeepOldKeysReservedAndInactiveNewKeys() {
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys, competitor;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        const auto candidate = preferences(directory.filePath("history"), "Ctrl+2");
        apply.apply(candidate);
        QSignalSpy activated(&keys, &Hotkeys::activated);
        const int originalId = nativeIdForTest(QKeySequence("Ctrl+1"));
        const int reservedId = nativeIdForTest(QKeySequence("Ctrl+2"));
        QVERIFY(originalId && reservedId);
        QVERIFY(keys.dispatchNative(originalId));
        QVERIFY(!keys.dispatchNative(reservedId));
        QCOMPARE(activated.count(), 1);
        QVERIFY(apply.busy());
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1"));
        QCOMPARE(apply.settings(), initial);
        QVERIFY(!competitor.bind(0, QKeySequence("Ctrl+1")));
        QVERIFY(!competitor.bind(0, QKeySequence("Ctrl+2")));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished[0][0].toBool());
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+2"));
        QVERIFY(!keys.dispatchNative(originalId));
        QVERIFY(keys.dispatchNative(reservedId));
        QCOMPARE(activated.count(), 2);
        QCOMPARE(apply.settings(), candidate);
        QCOMPARE(readJson(directory.filePath("settings.json")), candidate);
        QVERIFY(competitor.bind(0, QKeySequence("Ctrl+1")));
    }
    void independentChangesMergeWhileDirectoryMigrationIsPending() {
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("old"));
        const auto destination = directory.filePath("new");
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        apply.apply({{"history", QJsonObject{{"dir", destination}}}});
        QVERIFY(apply.busy());
        apply.apply({{"pin", QJsonObject{{"confirmDoubleClickClose", false}}}});
        apply.apply({{"appearance", QJsonObject{{"theme", "dark"}}}});
        apply.apply({{"appearance", QJsonObject{{"theme", "light"}}}});
        QTRY_VERIFY(finished.count() == 2 && !apply.busy());
        const auto saved = readJson(directory.filePath("settings.json"));
        QCOMPARE(saved, apply.settings());
        QCOMPARE(saved.value("history").toObject().value("dir").toString(), destination);
        QCOMPARE(saved.value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(false));
        QCOMPARE(saved.value("appearance").toObject().value("theme").toString(), QString("light"));
    }
    void failedCommitNeverReleasesOriginalKeys_data() {
        QTest::addColumn<bool>("backup"); QTest::newRow("regular") << false; QTest::newRow("backup") << true;
    }
    void failedCommitNeverReleasesOriginalKeys() {
        QFETCH(bool, backup);
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys, competitor;
        SettingsApply apply(initial, directory.path(), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        const auto candidate = preferences(directory.filePath("new"), "Ctrl+2");
        if (backup) { QString error; QVERIFY2(apply.applyBackup(candidate, &error), qPrintable(error)); }
        else apply.apply(candidate);
        QVERIFY(!competitor.bind(0, QKeySequence("Ctrl+1")));
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished[0][0].toBool());
        QCOMPARE(apply.settings(), initial);
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1"));
        QVERIFY(!competitor.bind(0, QKeySequence("Ctrl+1")));
        QVERIFY(competitor.bind(1, QKeySequence("Ctrl+2")));
    }
    void conflictingKeyKeepsOldValueWhileOtherSettingsSave() {
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        auto candidate = preferences(directory.filePath("history"), "Ctrl+F12");
        candidate["ocr"] = QJsonObject{{"language", "en-US"}};
        apply.apply(candidate);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished[0][0].toBool());
        QCOMPARE(apply.settings().value("hotkeys"), initial.value("hotkeys"));
        QCOMPARE(apply.settings().value("ocr"), candidate.value("ocr"));
        QCOMPARE(finished[0][2].toStringList(), QStringList{hotkeyLabels[0]});
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1"));
    }
    void clipboardCheckpointFailureKeepsBothHistoriesAndHotkeys_data() {
        QTest::addColumn<bool>("backup"); QTest::newRow("regular") << false; QTest::newRow("backup") << true;
    }
    void clipboardCheckpointFailureKeepsBothHistoriesAndHotkeys() {
        QFETCH(bool, backup);
        QTemporaryDir directory;
        const QString oldClips = directory.filePath("clips"), nextClips = directory.filePath("new-clips");
        const QString oldHistory = directory.filePath("history"), nextHistory = directory.filePath("new-history");
        auto initial = preferences(oldHistory); initial["clipboard"] = QJsonObject{{"dir", oldClips}};
        ClipboardArchive clipboard(initial.value("clipboard").toObject());
        QMimeData mime; mime.setText("keep clipboard"); clipboard.add(captureClipboard(mime));
        QTRY_COMPARE(clipboard.entries().size(), 1);
        HistoryStore history(initial.value("history").toObject());
        QImage picture(30, 30, QImage::Format_ARGB32); picture.fill(Qt::white); history.remember(picture);
        QTRY_COMPARE(historyFiles(oldHistory).size(), 1);
        Hotkeys keys, competitor;
        SettingsApply apply(initial, directory.path(), keys, history, nullptr, &clipboard);
        auto candidate = preferences(nextHistory, "Ctrl+2"); candidate["clipboard"] = QJsonObject{{"dir", nextClips}};
        QSignalSpy finished(&apply, &SettingsApply::finished);
        if (backup) { QString error; QVERIFY2(apply.applyBackup(candidate, &error), qPrintable(error)); }
        else apply.apply(candidate);
        QTRY_COMPARE(finished.count(), 1); QVERIFY(!finished[0][0].toBool());
        QCOMPARE(apply.settings(), initial); QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+1"));
        QVERIFY(!competitor.bind(0, QKeySequence("Ctrl+1")));
        QCOMPARE(historyFiles(oldHistory).size(), 1);
        QVERIFY(QFile::exists(QDir(oldClips).filePath(clipboard.entries().first().id + ".clip")));
    }
    void lockedClipboardDoesNotPreventSavingAppearance() {
        QTemporaryDir directory;
        const auto clips = directory.filePath("clips");
        auto initial = preferences(directory.filePath("history"));
        initial["clipboard"] = QJsonObject{{"dir", clips}};
        ClipboardArchive owner(initial.value("clipboard").toObject());
        QSignalSpy ownerReady(&owner, &ClipboardArchive::changed);
        QTRY_VERIFY(!ownerReady.isEmpty());
        ClipboardArchive blocked(initial.value("clipboard").toObject());
        QSignalSpy blockedError(&blocked, &ClipboardArchive::errorOccurred);
        QTRY_VERIFY(!blockedError.isEmpty());
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history, nullptr, &blocked);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        apply.apply({{"appearance", QJsonObject{{"theme", "dark"}}}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first()[0].toBool());
        QCOMPARE(readJson(directory.filePath("settings.json")).value("appearance").toObject().value("theme").toString(), QString("dark"));
    }
    void destructionDrainsChangesAfterFailedMigration() {
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("history"));
        Hotkeys keys;
        int failures = 0, successes = 0;
        {
            HistoryStore history(initial.value("history").toObject());
            SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
            connect(&apply, &SettingsApply::finished, this, [&](bool success) { success ? ++successes : ++failures; });
            apply.apply({{"history", QJsonObject{{"dir", "relative-history"}}}});
            apply.apply({{"ocr", QJsonObject{{"language", "de-DE"}}}});
        }
        QCOMPARE(failures, 1);
        QCOMPARE(successes, 1);
        const auto saved = readJson(directory.filePath("settings.json"));
        QCOMPARE(saved.value("history"), initial.value("history"));
        QCOMPARE(saved.value("ocr").toObject().value("language").toString(), QString("de-DE"));
    }
};

QTEST_GUILESS_MAIN(SettingsApplyTest)
#include "settings_apply_test.moc"
