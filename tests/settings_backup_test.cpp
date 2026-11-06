#include "settings/settings_backup.h"
#include "settings/settings_apply.h"
#include "storage/store.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace still;

static QByteArray bytes(const QJsonObject &document) { return QJsonDocument(document).toJson(); }
static QJsonObject changed(const QString &group, const QString &key, const QJsonValue &value) {
    auto document = settingsBackupDocument({}); auto settings = document.value("settings").toObject();
    auto fields = settings.value(group).toObject(); fields[key] = value; settings[group] = fields; document["settings"] = settings;
    return document;
}

class SettingsBackupTest : public QObject {
    Q_OBJECT
private slots:
    void defaultsUseTheActualPlatformHotkeysAndActiveHiddenSettings() {
        const auto settings = defaultSettings();
        QCOMPARE(settings.value("hotkeys").toObject(), defaultHotkeys());
        QCOMPARE(settings.value("hotkeys").toObject().value("capture").toString(), QString("Ctrl+1"));
#ifdef Q_OS_WIN
        QCOMPARE(settings.value("hotkeys").toObject().value("clipboardHistory").toString(), QString("Ctrl+Alt+Space"));
#else
        QCOMPARE(settings.value("hotkeys").toObject().value("clipboardHistory").toString(), QString("Ctrl+Shift+Space"));
#endif
        QCOMPARE(settings.value("capture").toObject().value("maxEdge").toInt(-1), 0);
        QVERIFY(settings.value("pin").toObject().value("confirmDoubleClickClose").toBool());
        QCOMPARE(settings.value("pin").toObject().value("opacity").toDouble(), 1.0);
        QCOMPARE(settings.value("scroll").toObject().value("maxHeightPx").toInt(), 24000);
        QCOMPARE(settings.value("scroll").toObject().value("maxFrames").toInt(), 80);
        for (const auto &group : {"save", "history", "clipboard"}) QCOMPARE(settings.value(group).toObject().value("dir"), QJsonValue(""));
        QString error; const auto parsed = parseSettingsBackup(bytes(settingsBackupDocument({})), &error);
        QVERIFY2(parsed, qPrintable(error)); QCOMPARE(*parsed, settings);
    }
    void roundTripKeepsAllSupportedValuesWithoutUnknownOrInternalFields() {
        auto settings = defaultSettings();
        settings["capture"] = QJsonObject{{"maxEdge", 1234}, {"showMagnifier", false}, {"snapToWindows", false}, {"afterSelectAction", "pin"}};
        auto pin = settings.value("pin").toObject(); pin["opacity"] = 0.45; pin["confirmDoubleClickClose"] = false; pin["futureToken"] = "do not export"; settings["pin"] = pin;
        auto scroll = settings.value("scroll").toObject(); scroll["maxHeightPx"] = 40000; scroll["maxFrames"] = 120; settings["scroll"] = scroll;
        auto save = settings.value("save").toObject(); save["nameTemplate"] = QStringLiteral("项目_{date}_{ms}"); save["dir"] = QStringLiteral("/data/项目"); settings["save"] = save;
        settings["future"] = QJsonObject{{"apiKey", "never export"}}; settings["clipboardShortcutMigrated"] = true;
        const auto document = settingsBackupDocument(settings); const auto exported = document.value("settings").toObject();
        QCOMPARE(document.value("format"), QJsonValue("still-settings")); QCOMPARE(document.value("version"), QJsonValue(1));
        QVERIFY(!exported.contains("future")); QVERIFY(!exported.contains("clipboardShortcutMigrated")); QVERIFY(!exported.value("pin").toObject().contains("futureToken"));
        settings.remove("future"); settings.remove("clipboardShortcutMigrated"); pin.remove("futureToken"); settings["pin"] = pin;
        QString error; const auto parsed = parseSettingsBackup(bytes(document), &error); QVERIFY2(parsed, qPrintable(error)); QCOMPARE(*parsed, settings);
        QSet<QString> fields;
        for (const auto &value : backupSettingRows()) {
            const auto row = value.toMap(); const auto name = row.value("group").toString() + '.' + row.value("key").toString();
            QVERIFY(!row.value("key").toString().isEmpty()); QVERIFY(!fields.contains(name)); fields.insert(name);
            QVERIFY(exported.value(row.value("group").toString()).toObject().contains(row.value("key").toString()));
        }
    }
    void exportUsesEffectiveHiddenBoundsAndResolvesExistingRelativeDirectories() {
        const QJsonObject current{{"capture", QJsonObject{{"maxEdge", -20}}}, {"pin", QJsonObject{{"opacity", 5}, {"confirmDoubleClickClose", "false"}}},
            {"scroll", QJsonObject{{"maxHeightPx", 200}, {"maxFrames", 999}}}, {"save", QJsonObject{{"dir", "relative-save"}}}};
        const auto exported = settingsBackupDocument(current).value("settings").toObject();
        QCOMPARE(exported.value("capture").toObject().value("maxEdge").toInt(), 0);
        QCOMPARE(exported.value("pin").toObject().value("opacity").toDouble(), 1.0);
        QVERIFY(exported.value("pin").toObject().value("confirmDoubleClickClose").toBool());
        QCOMPARE(exported.value("scroll").toObject().value("maxHeightPx").toInt(), 1000);
        QCOMPARE(exported.value("scroll").toObject().value("maxFrames").toInt(), 300);
        QCOMPARE(exported.value("save").toObject().value("dir").toString(), QDir("relative-save").absolutePath());
        QCOMPARE(current.value("save").toObject().value("dir").toString(), QString("relative-save"));
    }
    void malformedDocumentsAreRejectedWhole_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("empty") << QByteArray(); QTest::newRow("truncated") << QByteArray("{\"format\":");
        QTest::newRow("array") << QByteArray("[]"); QTest::newRow("null") << QByteArray("null");
        auto add = [](const QByteArray &name, const QJsonObject &document) { QTest::newRow(name.constData()) << bytes(document); };
        for (const auto &version : {QJsonValue(2), QJsonValue(0), QJsonValue(1.5), QJsonValue("1"), QJsonValue(true), QJsonValue()}) {
            auto document = settingsBackupDocument({}); document["version"] = version; add("version-" + QByteArray::number(int(version.type())) + '-' + QByteArray::number(version.toDouble()), document);
        }
        for (const auto &key : {"format", "version", "settings"}) { auto document = settingsBackupDocument({}); document.remove(key); add(QByteArray("missing-") + key, document); }
        auto document = settingsBackupDocument({}); document["format"] = "other-app"; add("wrong-format", document);
        document = settingsBackupDocument({}); document["extra"] = true; add("unknown-envelope-field", document);
        document = settingsBackupDocument({}); document["settings"] = QJsonArray{}; add("settings-array", document);
        auto settings = defaultSettings(); settings["future"] = QJsonObject{{"token", "x"}}; document = settingsBackupDocument({}); document["settings"] = settings; add("unknown-group", document);
        for (const auto &group : defaultSettings().keys()) {
            settings = defaultSettings(); settings.remove(group); document["settings"] = settings; add("missing-group-" + group.toUtf8(), document);
            settings = defaultSettings(); settings[group] = false; document["settings"] = settings; add("non-object-group-" + group.toUtf8(), document);
        }
        for (const auto &value : backupSettingRows()) {
            const auto row = value.toMap(); const auto group = row.value("group").toString(), key = row.value("key").toString();
            settings = defaultSettings(); auto fields = settings.value(group).toObject(); fields.remove(key); settings[group] = fields; document["settings"] = settings;
            add("missing-field-" + group.toUtf8() + '-' + key.toUtf8(), document);
        }
        settings = defaultSettings(); auto fields = settings.value("pin").toObject(); fields["extra"] = true; settings["pin"] = fields; document["settings"] = settings; add("unknown-setting", document);
    }
    void malformedDocumentsAreRejectedWhole() {
        QFETCH(QByteArray, input); QString error;
        QVERIFY(!parseSettingsBackup(input, &error)); QVERIFY(!error.isEmpty());
    }
    void invalidValuesAreRejectedInsteadOfNormalized_data() {
        QTest::addColumn<QByteArray>("input");
        auto add = [](const QString &name, const QString &group, const QString &key, const QJsonValue &value) { QTest::newRow(name.toUtf8().constData()) << bytes(changed(group, key, value)); };
        for (const auto &value : backupSettingRows()) {
            const auto row = value.toMap(); const auto type = row.value("type").toString(), group = row.value("group").toString(), key = row.value("key").toString();
            const auto name = group + '-' + key;
            add(name + "-wrong-type", group, key, type == "switch" ? QJsonValue("false") : QJsonValue(false));
            if (type == "number" || type == "decimal") {
                add(name + "-underflow", group, key, row.value("minimum").toDouble() - 1);
                add(name + "-overflow", group, key, row.value("maximum").toDouble() + 1);
                add(name + "-numeric-string", group, key, QString::number(row.value("defaultValue").toDouble()));
                if (type == "number") add(name + "-fraction", group, key, row.value("minimum").toDouble() + 0.5);
            }
            if (type == "choice") add(name + "-unknown-choice", group, key, "unknown");
        }
        add("relative-directory", "history", "dir", "relative/history"); add("windows-relative-directory", "save", "dir", "C:relative");
        add("directory-nul", "clipboard", "dir", QString("/tmp/a") + QChar::Null + 'b');
        add("template-path", "save", "nameTemplate", "../image"); add("text-nul", "ocr", "language", QString("en") + QChar::Null);
        for (const auto &sequence : {"Ctrl+DefinitelyNotAKey", "Ctrl+K, Ctrl+C", "Ctrl", "Shift", "Meta", "Alt", "Ctrl+"}) add(QString("invalid-shortcut-") + sequence, "hotkeys", "capture", sequence);
    }
    void invalidValuesAreRejectedInsteadOfNormalized() {
        QFETCH(QByteArray, input); QString error;
        QVERIFY(!parseSettingsBackup(input, &error)); QVERIFY(!error.isEmpty());
    }
    void duplicateShortcutsUseParsedSequencesAndEmptyDisablesAreAllowed() {
        auto document = changed("hotkeys", "capture", "Shift+Ctrl+K"); auto settings = document.value("settings").toObject(); auto keys = settings.value("hotkeys").toObject();
        keys["quickCapture"] = "Ctrl+Shift+K"; settings["hotkeys"] = keys; document["settings"] = settings; QString error;
        QVERIFY(!parseSettingsBackup(bytes(document), &error)); QVERIFY(error.contains(hotkeyLabels[1]));
        keys["capture"] = ""; keys["quickCapture"] = ""; settings["hotkeys"] = keys; document["settings"] = settings;
        QVERIFY2(parseSettingsBackup(bytes(document), &error), qPrintable(error));
    }
    void foreignDirectoryPathsAreMetadataWithoutChangingTheCurrentMachine() {
        for (const auto &path : {"/Volumes/Data/Still", "C:\\Data\\Still", "D:/Data/Still", "\\\\server\\share\\Still"}) {
            QString error; const auto parsed = parseSettingsBackup(bytes(changed("history", "dir", path)), &error);
            QVERIFY2(parsed, qPrintable(error)); QCOMPARE(parsed->value("history").toObject().value("dir").toString(), QString(path));
        }
    }
    void savingIsAtomicAndCreatesAParseableVersionedFile() {
        QTemporaryDir directory; const auto path = directory.filePath("nested/preferences.json"); QString error;
        auto settings = defaultSettings(); auto appearance = settings.value("appearance").toObject(); appearance["theme"] = "dark"; settings["appearance"] = appearance;
        QVERIFY2(saveSettingsBackup(path, settings, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        const auto parsed = parseSettingsBackup(before, &error); QVERIFY2(parsed, qPrintable(error)); QCOMPARE(*parsed, settings);
        auto keys = settings.value("hotkeys").toObject(); keys["capture"] = keys.value("quickCapture"); settings["hotkeys"] = keys;
        QVERIFY(!saveSettingsBackup(path, settings, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        const auto parentFile = directory.filePath("occupied"); QFile occupied(parentFile); QVERIFY(occupied.open(QIODevice::WriteOnly)); QCOMPARE(occupied.write("keep"), qint64(4)); occupied.close();
        QVERIFY(!saveSettingsBackup(parentFile + "/preferences.json", {}, &error));
        QVERIFY(occupied.open(QIODevice::ReadOnly)); QCOMPARE(occupied.readAll(), QByteArray("keep"));
    }
};

QTEST_GUILESS_MAIN(SettingsBackupTest)
#include "settings_backup_test.moc"
