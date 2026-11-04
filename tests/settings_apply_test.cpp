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

using namespace pixclone;
namespace pixclone { int nativeIdForTest(const QKeySequence &sequence); }

static QJsonObject preferences(const QString &directory, const QString &key = "Ctrl+1") {
    return {{"hotkeys", QJsonObject{{"capture", key}}}, {"history", QJsonObject{{"dir", directory}}}};
}

class SettingsApplyTest : public QObject {
    Q_OBJECT
private slots:
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
    void failedCommitNeverReleasesOriginalKeys() {
        QTemporaryDir directory;
        const auto initial = preferences(directory.filePath("history"));
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys, competitor;
        SettingsApply apply(initial, directory.path(), keys, history);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        apply.apply(preferences(directory.filePath("new"), "Ctrl+2"));
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
    void clipboardCheckpointFailureKeepsBothHistoriesAndHotkeys() {
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
        QSignalSpy finished(&apply, &SettingsApply::finished); apply.apply(candidate);
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
};

QTEST_GUILESS_MAIN(SettingsApplyTest)
#include "settings_apply_test.moc"
