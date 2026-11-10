#include "settings/settings_draft.h"
#include "settings/settings_apply.h"
#include "storage/history.h"
#include "storage/store.h"
#include "storage/image_export.h"
#include "platform/platform.h"
#include "ui/i18n.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace still;

class SettingsDraftTest : public QObject {
    Q_OBJECT
private slots:
    void directoryInputRejectsDependentPaths_data() {
        QTest::addColumn<QString>("group"); QTest::addColumn<QString>("path");
        QStringList paths{"relative-folder"};
#ifdef Q_OS_WIN
        paths.append({"C:relative-folder", "/rooted-folder", "\\rooted-folder", "\\\\server"});
#endif
        for (const auto &group : {QString("save"), QString("history"), QString("clipboard")})
            for (const auto &path : paths) QTest::newRow(qPrintable(group + '-' + path)) << group << path;
    }
    void directoryInputRejectsDependentPaths() {
        QFETCH(QString, group); QFETCH(QString, path);
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        SettingsDraft draft(apply);
        draft.setSetting(group, "dir", path);
        QVERIFY(!apply.busy()); QVERIFY(draft.statusError()); QCOMPARE(apply.settings(), initial);
        QVERIFY(!QFile::exists(directory.filePath("settings.json")));
        draft.setSetting(group, "dir", initial.value(group).toObject().value("dir").toString());
        QVERIFY(!draft.statusError());
    }
    void normalizationValidatesKnownFieldsAndKeepsUnknownSettings() {
        const auto form = normalizeSettings({{"clipboard", QJsonObject{{"maxItems", 0}, {"enabled", "false"}}},
            {"capture", QJsonObject{{"afterSelectAction", "unknown"}}}, {"future", QJsonObject{{"keep", 42}}}});
        QCOMPARE(form.value("clipboard").toObject().value("maxItems").toInt(), 200);
        QVERIFY(form.value("clipboard").toObject().value("enabled").toBool());
        QCOMPARE(form.value("capture").toObject().value("afterSelectAction").toString(), QString("annotate"));
        QCOMPARE(form.value("future").toObject().value("keep").toInt(), 42);
    }
    void completedSavePreservesNewerDraftAndCommitAppliesIt() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        SettingsDraft draft(apply);
        QSignalSpy finished(&apply, &SettingsApply::finished);
        draft.setSetting("ocr", "language", "en-US");
        draft.setDraft("ocr", "language", "new unfinished input");
        QTRY_VERIFY(!finished.isEmpty() && !apply.busy());
        QCOMPARE(apply.settings().value("ocr").toObject().value("language").toString(), QString("en-US"));
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("new unfinished input"));
        draft.commitDrafts();
        QTRY_VERIFY(!apply.busy());
        QCOMPARE(readJson(directory.filePath("settings.json")).value("ocr").toObject().value("language").toString(), QString("new unfinished input"));
        QVERIFY(!draft.statusError());
    }
    void imageExportSettingsValidateAndPersist() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}},
            {"save", QJsonObject{{"nameTemplate", "../escape"}, {"format", "unknown"}, {"jpegQuality", 101}, {"pngCompression", -1}}}});
        const auto options = initial.value("save").toObject();
        QVERIFY(!options.value("copyAsFile").toBool());
        QCOMPARE(options.value("nameTemplate").toString(), defaultImageNameTemplate);
        QCOMPARE(options.value("format").toString(), QString("png"));
        QCOMPARE(options.value("jpegQuality").toInt(), 95); QCOMPARE(options.value("pngCompression").toInt(), 50);
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        SettingsDraft draft(apply);
        draft.setDraft("save", "nameTemplate", "../bad"); draft.commitDrafts();
        QVERIFY(draft.statusError()); QVERIFY(!apply.busy());
        QCOMPARE(apply.settings().value("save").toObject(), options);
        draft.setSetting("save", "nameTemplate", "work_{date}");
        draft.setSetting("save", "format", "jpeg");
        draft.setSetting("save", "jpegQuality", 83);
        draft.setSetting("save", "pngCompression", 72);
        draft.setSetting("save", "copyAsFile", true);
        QTRY_VERIFY(!apply.busy());
        const auto saved = readJson(directory.filePath("settings.json")).value("save").toObject();
        QCOMPARE(saved.value("nameTemplate").toString(), QString("work_{date}"));
        QCOMPARE(saved.value("format").toString(), QString("jpeg"));
        QCOMPARE(saved.value("jpegQuality").toInt(), 83); QCOMPARE(saved.value("pngCompression").toInt(), 72);
        QVERIFY(saved.value("copyAsFile").toBool());
        QVERIFY(!draft.statusError());
    }
    void queuedChangesKeepTheLatestValue() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        SettingsDraft draft(apply);
        draft.setSetting("clipboard", "maxItems", 42);
        draft.setSetting("clipboard", "maxItems", 43);
        draft.setDraft("ocr", "language", "unfinished");
        QTRY_VERIFY(!apply.busy());
        QCOMPARE(draft.form().value("clipboard").toObject().value("maxItems").toInt(), 43);
        QCOMPARE(apply.settings().value("clipboard").toObject().value("maxItems").toInt(), 43);
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
    void failedSaveRollsBackSubmittedValuesButKeepsNewerInput() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.path(), keys, history);
        SettingsDraft draft(apply);
        draft.setSetting("ocr", "language", "en-US");
        draft.setDraft("ocr", "language", "unfinished");
        draft.setSetting("clipboard", "enabled", false);
        QTRY_VERIFY(!apply.busy());
        QVERIFY(draft.statusError());
        QCOMPARE(apply.settings(), initial);
        QVERIFY(draft.form().value("clipboard").toObject().value("enabled").toBool());
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
    void destroyingDraftDoesNotCancelAcceptedSave() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject());
        Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        auto *draft = new SettingsDraft(apply);
        draft->setSetting("ocr", "language", "en-US");
        delete draft;
        QTRY_VERIFY(!apply.busy());
        QCOMPARE(readJson(directory.filePath("settings.json")).value("ocr").toObject().value("language").toString(), QString("en-US"));
    }
    void bulkRestoreReplacesUnfinishedInputWithoutSubmittingItAgain() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history);
        SettingsDraft draft(apply); draft.setDraft("ocr", "language", "unfinished");
        QString error; QVERIFY2(apply.applyBackup({{"ocr", QJsonObject{{"language", "en-US"}}}}, &error), qPrintable(error));
        QTRY_VERIFY(!apply.busy());
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
        draft.reloadSaved();
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("en-US"));
        draft.commitDrafts(); QVERIFY(!apply.busy());
        QCOMPARE(readJson(directory.filePath("settings.json")).value("ocr").toObject().value("language").toString(), QString("en-US"));
    }
    void visibleRowsUseRealShortcutDefaultsWithoutChangingNormalization() {
        const auto normalized = normalizeSettings({}).value("hotkeys").toObject();
        const auto defaults = defaultHotkeys();
        for (const auto &name : hotkeyNames) QCOMPARE(normalized.value(name).toString(), QString());
        for (const auto &value : SettingsDraft::rows(2)) {
            const auto row = value.toMap(); const auto key = row.value("key").toString();
            if (!key.isEmpty()) QCOMPARE(row.value("defaultValue").toString(), defaults.value(key).toString());
        }
    }
    void restoringOneFieldKeepsOtherSettingsAndUnfinishedInput_data() {
        QTest::addColumn<QString>("group"); QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("changed"); QTest::addColumn<QVariant>("restored");
        QTest::newRow("switch") << QString("capture") << QString("showMagnifier") << QVariant(false) << QVariant(true);
        QTest::newRow("choice") << QString("capture") << QString("afterSelectAction") << QVariant(QString("copy")) << QVariant(QString("annotate"));
        QTest::newRow("template") << QString("save") << QString("nameTemplate") << QVariant(QString("work_{date}")) << QVariant(defaultImageNameTemplate);
        QTest::newRow("number") << QString("save") << QString("jpegQuality") << QVariant(60) << QVariant(95);
        QTest::newRow("text") << QString("ocr") << QString("language") << QVariant(QString("en-US")) << QVariant(QString("zh-Hans-CN"));
    }
    void restoringOneFieldKeepsOtherSettingsAndUnfinishedInput() {
        QFETCH(QString, group); QFETCH(QString, key); QFETCH(QVariant, changed); QFETCH(QVariant, restored);
        QTemporaryDir directory;
        auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}},
            {"pin", QJsonObject{{"opacity", 0.4}}}, {"future", QJsonObject{{"keep", 42}}}});
        auto fields = initial.value(group).toObject(); fields[key] = QJsonValue::fromVariant(changed); initial[group] = fields;
        const auto path = directory.filePath("settings.json"); QVERIFY(writeJson(path, initial));
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, path, keys, history); SettingsDraft draft(apply);
        draft.setDraft("save", "dir", "unfinished-directory");
        draft.restoreDefault(group, key); QTRY_VERIFY(!apply.busy());
        auto expected = initial; fields[key] = QJsonValue::fromVariant(restored); expected[group] = fields;
        QCOMPARE(apply.settings(), expected); QCOMPARE(readJson(path), expected);
        QCOMPARE(draft.form().value(group).toObject().value(key), QJsonValue::fromVariant(restored));
        QCOMPARE(draft.form().value("save").toObject().value("dir").toString(), QString("unfinished-directory"));
    }
    void restoringAnAlreadyDefaultFieldOnlyDiscardsItsOwnDraft() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}, {"future", QJsonObject{{"keep", 42}}}});
        const auto path = directory.filePath("settings.json"); QVERIFY(writeJson(path, initial));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, path, keys, history); SettingsDraft draft(apply); QSignalSpy finished(&apply, &SettingsApply::finished);
        draft.setDraft("ocr", "language", "unfinished"); draft.setDraft("save", "nameTemplate", "draft_{ms}");
        draft.restoreDefault("save", "nameTemplate"); draft.restoreDefault("save", "nameTemplate");
        draft.restoreDefault("future", "keep");
        QVERIFY(!apply.busy()); QVERIFY(finished.isEmpty()); QCOMPARE(apply.settings(), initial);
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), defaultImageNameTemplate);
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
    void restoringAnInvalidTemplateDraftClearsItsErrorWithoutSavingAgain() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        const auto path = directory.filePath("settings.json"); QVERIFY(writeJson(path, initial));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, path, keys, history); SettingsDraft draft(apply); QSignalSpy finished(&apply, &SettingsApply::finished);
        draft.setDraft("save", "nameTemplate", "../bad"); draft.commitDrafts();
        QVERIFY(draft.statusError()); QVERIFY(!apply.busy()); QVERIFY(finished.isEmpty());
        draft.setDraft("ocr", "language", "unfinished"); draft.restoreDefault("save", "nameTemplate");
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), defaultImageNameTemplate);
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
        QVERIFY(!draft.statusError()); QVERIFY(!apply.busy()); QVERIFY(finished.isEmpty()); QCOMPARE(apply.settings(), initial);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
    void restoringAnotherDefaultFieldKeepsTheTemplateError() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history); SettingsDraft draft(apply);
        draft.setDraft("save", "nameTemplate", "../bad"); draft.commitDrafts(); const auto error = draft.status();
        QVERIFY(draft.statusError()); draft.restoreDefault("capture", "showMagnifier");
        QVERIFY(draft.statusError()); QCOMPARE(draft.status(), error); QVERIFY(!apply.busy()); QCOMPARE(apply.settings(), initial);
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), QString("../bad"));
    }
    void restoringItsValidationErrorKeepsAnotherPendingSaveVisible() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.filePath("settings.json"), keys, history); SettingsDraft draft(apply); QSignalSpy finished(&apply, &SettingsApply::finished);
        draft.setSetting("capture", "showMagnifier", false); QVERIFY(apply.busy());
        draft.setDraft("save", "nameTemplate", "../bad"); draft.commitDrafts(); QVERIFY(draft.statusError());
        draft.setDraft("ocr", "language", "unfinished"); draft.restoreDefault("save", "nameTemplate");
        QVERIFY(apply.busy()); QVERIFY(!draft.statusError()); QCOMPARE(draft.status(), t("正在保存…"));
        QTRY_VERIFY(!apply.busy()); QCOMPARE(finished.count(), 1);
        QVERIFY(!apply.settings().value("capture").toObject().value("showMagnifier").toBool());
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), defaultImageNameTemplate);
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
    void completedBackendFailureIsNotClearedByRestoringADefaultTemplate() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.path(), keys, history); SettingsDraft draft(apply);
        draft.setSetting("capture", "showMagnifier", false); QVERIFY(apply.busy());
        draft.setDraft("save", "nameTemplate", "../bad"); draft.commitDrafts(); QVERIFY(draft.statusError());
        draft.setDraft("ocr", "language", "unfinished"); QTRY_VERIFY(!apply.busy());
        QVERIFY(draft.statusError()); const auto backendError = draft.status();
        QVERIFY(backendError.startsWith(t("更改未生效，已恢复原设置：")));
        draft.restoreDefault("save", "nameTemplate");
        QVERIFY(draft.statusError()); QCOMPARE(draft.status(), backendError); QCOMPARE(apply.settings(), initial);
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), defaultImageNameTemplate);
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
    void resettingDuringItsPendingSaveKeepsTheDefaultAfterRepeatedClicks() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}},
            {"save", QJsonObject{{"nameTemplate", "original_{date}"}}}});
        const auto path = directory.filePath("settings.json"); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, path, keys, history); SettingsDraft draft(apply); QSignalSpy finished(&apply, &SettingsApply::finished);
        draft.setSetting("save", "nameTemplate", "new_{ms}"); QVERIFY(apply.busy());
        draft.restoreDefault("save", "nameTemplate"); draft.restoreDefault("save", "nameTemplate");
        draft.setDraft("ocr", "language", "unfinished");
        QTRY_VERIFY(!apply.busy()); QCOMPARE(finished.count(), 2);
        auto expected = initial; auto save = expected.value("save").toObject(); save["nameTemplate"] = defaultImageNameTemplate; expected["save"] = save;
        QCOMPARE(apply.settings(), expected); QCOMPARE(readJson(path), expected);
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), defaultImageNameTemplate);
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
    void restoringAShortcutUsesItsActualPlatformDefault_data() {
        QTest::addColumn<QString>("name"); QTest::newRow("capture") << QString("capture"); QTest::newRow("clipboard") << QString("clipboardHistory");
    }
    void restoringAShortcutUsesItsActualPlatformDefault() {
        QFETCH(QString, name); QTemporaryDir directory;
        auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        auto bindings = initial.value("hotkeys").toObject(); bindings[name] = "Ctrl+9"; initial["hotkeys"] = bindings;
        const auto path = directory.filePath("settings.json"); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, path, keys, history); SettingsDraft draft(apply);
        draft.restoreDefault("hotkeys", name); QTRY_VERIFY(!apply.busy());
        const auto value = defaultHotkeys().value(name); QVERIFY(!value.toString().isEmpty());
        auto expected = initial; bindings[name] = value; expected["hotkeys"] = bindings;
        QCOMPARE(apply.settings(), expected); QCOMPARE(readJson(path), expected);
        QCOMPARE(draft.form().value("hotkeys").toObject().value(name), value);
        QCOMPARE(keys.sequence(hotkeyNames.indexOf(name)), QKeySequence::fromString(value.toString(), QKeySequence::PortableText));
    }
    void conflictingDefaultShortcutKeepsItsOriginalValueAndOtherDraft() {
        QTemporaryDir directory;
        auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}}});
        auto bindings = initial.value("hotkeys").toObject(); bindings["capture"] = "Ctrl+9"; initial["hotkeys"] = bindings;
        const auto path = directory.filePath("settings.json"); QVERIFY(writeJson(path, initial));
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys, competitor;
        SettingsApply apply(initial, path, keys, history); SettingsDraft draft(apply);
        QVERIFY(competitor.bind(0, QKeySequence::fromString(defaultHotkeys().value("capture").toString(), QKeySequence::PortableText)));
        draft.setDraft("ocr", "language", "unfinished"); draft.restoreDefault("hotkeys", "capture"); QTRY_VERIFY(!apply.busy());
        QVERIFY(draft.statusError()); QCOMPARE(apply.settings(), initial); QCOMPARE(readJson(path), initial);
        QCOMPARE(draft.form().value("hotkeys").toObject().value("capture").toString(), QString("Ctrl+9"));
        QCOMPARE(keys.sequence(0), QKeySequence("Ctrl+9"));
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
    void failedDefaultSaveRollsBackOnlyTheRequestedField() {
        QTemporaryDir directory;
        const auto initial = normalizeSettings({{"history", QJsonObject{{"dir", directory.filePath("history")}}},
            {"save", QJsonObject{{"nameTemplate", "keep_{date}"}}}});
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply apply(initial, directory.path(), keys, history); SettingsDraft draft(apply); QSignalSpy finished(&apply, &SettingsApply::finished);
        draft.setDraft("ocr", "language", "unfinished"); draft.restoreDefault("save", "nameTemplate"); QTRY_VERIFY(!apply.busy());
        QCOMPARE(finished.count(), 1); QVERIFY(!finished.first()[0].toBool()); QVERIFY(draft.statusError()); QCOMPARE(apply.settings(), initial);
        QCOMPARE(draft.form().value("save").toObject().value("nameTemplate").toString(), QString("keep_{date}"));
        QCOMPARE(draft.form().value("ocr").toObject().value("language").toString(), QString("unfinished"));
    }
};
QTEST_MAIN(SettingsDraftTest)
#include "settings_draft_test.moc"
