#include "settings/settings_backup_dialog.h"
#include "settings/settings_backup.h"
#include "settings/settings_apply.h"
#include "storage/history.h"
#include "storage/store.h"
#include "platform/platform.h"
#include "ui/i18n.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cmath>

using namespace still;

static QJsonObject preferences(const QString &directory) {
    auto result = defaultSettings();
    for (const auto &group : {"save", "history", "clipboard"}) {
        auto values = result.value(group).toObject(); values["dir"] = directory + '/' + group; result[group] = values;
    }
    auto keys = result.value("hotkeys").toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it) it.value() = "";
    keys["capture"] = "Ctrl+9"; result["hotkeys"] = keys;
    result["future"] = QJsonObject{{"keep", 42}};
    return result;
}

static QPushButton *button(SettingsBackupDialog &dialog, const char *name) { return dialog.findChild<QPushButton *>(name); }
static QLabel *status(SettingsBackupDialog &dialog) { return dialog.findChild<QLabel *>("SettingsBackupStatus"); }
static QPlainTextEdit *preview(SettingsBackupDialog &dialog) { return dialog.findChild<QPlainTextEdit *>("SettingsBackupPreview"); }
static QCheckBox *directories(SettingsBackupDialog &dialog) { return dialog.findChild<QCheckBox *>("RestoreBackupDirectories"); }

static bool chooseFile(SettingsBackupDialog &dialog, const char *action, const QString &path, bool accept = true) {
    bool handled = false; QTimer answer; answer.setSingleShot(true);
    QObject::connect(&answer, &QTimer::timeout, &dialog, [&] {
        auto *chooser = dialog.findChild<QFileDialog *>(); Q_ASSERT(chooser);
        handled = true;
        if (accept) {
            chooser->setOption(QFileDialog::DontConfirmOverwrite); chooser->selectFile(path);
            static_cast<QDialog *>(chooser)->accept();
        } else chooser->reject();
    });
    answer.start(0); button(dialog, action)->click(); return handled;
}

struct Confirmation { bool defaultNo = false; QString text; };
static Confirmation confirm(SettingsBackupDialog &dialog, const char *action, QMessageBox::StandardButton reply) {
    Confirmation result; QTimer answer; answer.setSingleShot(true);
    QObject::connect(&answer, &QTimer::timeout, &dialog, [&] {
        auto *box = dialog.findChild<QMessageBox *>("SettingsBackupConfirmation"); Q_ASSERT(box);
        result.defaultNo = box->defaultButton() == box->button(QMessageBox::No);
        result.text = box->text(); QTest::mouseClick(box->button(reply), Qt::LeftButton);
    });
    answer.start(0); button(dialog, action)->click(); return result;
}

class SettingsBackupDialogTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
    void init() { InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", "light"}}); }
    void exportingUsesCommittedSettingsAndProtectsTheActiveFile() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); const QString path = directory.filePath("settings.json");
        QVERIFY(writeJson(path, initial)); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, path, keys, history); SettingsBackupDialog dialog(settings);
        QVERIFY(!dialog.testAttribute(Qt::WA_DeleteOnClose)); QVERIFY(!button(dialog, "ImportSettingsBackup")->isEnabled());
        QVERIFY(!directories(dialog)->isChecked()); QVERIFY(preview(dialog)->isReadOnly());
        const QString backup = directory.filePath("backup.json");
        QVERIFY(chooseFile(dialog, "ExportSettingsBackup", backup));
        QCOMPARE(readJson(backup), settingsBackupDocument(initial)); QCOMPARE(readJson(path), initial);
        QVERIFY(status(dialog)->text().contains(t("已导出配置备份。")));
        QVERIFY(chooseFile(dialog, "ExportSettingsBackup", path));
        QVERIFY(status(dialog)->text().contains(t("请选择其他文件，不能覆盖正在使用的配置。")));
        QCOMPARE(readJson(path), initial); QCOMPARE(settings.settings(), initial);
    }
    void cancellingFileSelectionAndConfirmationNeverChangesSettings() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); const QString path = directory.filePath("settings.json");
        QVERIFY(writeJson(path, initial)); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, path, keys, history); SettingsBackupDialog dialog(settings);
        QVERIFY(chooseFile(dialog, "ExportSettingsBackup", {}, false)); QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", {}, false));
        QVERIFY(!button(dialog, "ImportSettingsBackup")->isEnabled());
        const auto answer = confirm(dialog, "RestoreDefaultSettings", QMessageBox::No);
        QVERIFY(answer.defaultNo); QVERIFY(answer.text.contains(QStringLiteral("配额"))); QVERIFY(answer.text.contains(QStringLiteral("未保存")));
        QCOMPARE(settings.settings(), initial); QCOMPARE(readJson(path), initial); QVERIFY(!settings.busy());
    }
    void exportingCannotCreateTheActiveFileThroughDifferentCase() {
        QTemporaryDir directory; QFile probe(directory.filePath("case-probe"));
        QVERIFY(probe.open(QIODevice::WriteOnly)); probe.write("probe"); probe.close();
        if (!QFile::exists(directory.filePath("CASE-PROBE"))) QSKIP("Case-sensitive volume keeps these as distinct paths");
        QVERIFY(probe.remove());
        const auto initial = preferences(directory.path()); const QString active = directory.filePath("qt-settings.json");
        QVERIFY(!QFile::exists(active)); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, active, keys, history); SettingsBackupDialog dialog(settings);
        QVERIFY(chooseFile(dialog, "ExportSettingsBackup", directory.filePath("QT-SETTINGS.JSON")));
        QVERIFY(status(dialog)->text().contains(t("请选择其他文件，不能覆盖正在使用的配置。")));
        QVERIFY(!QFile::exists(active)); QCOMPARE(settings.settings(), initial);
    }
    void exportingAllowsDifferentCaseOnACaseSensitiveVolume() {
#ifdef Q_OS_WIN
        QSKIP("Windows uses case-insensitive settings filename protection");
#endif
        QTemporaryDir directory; QFile probe(directory.filePath("case-probe"));
        QVERIFY(probe.open(QIODevice::WriteOnly)); probe.write("probe"); probe.close();
        if (QFile::exists(directory.filePath("CASE-PROBE"))) QSKIP("Case-insensitive volume aliases these filenames");
        QVERIFY(probe.remove());
        const auto initial = preferences(directory.path()); const QString active = directory.filePath("qt-settings.json"), backup = directory.filePath("QT-SETTINGS.JSON");
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, active, keys, history); SettingsBackupDialog dialog(settings);
        QVERIFY(chooseFile(dialog, "ExportSettingsBackup", backup));
        QCOMPARE(readJson(backup), settingsBackupDocument(initial)); QVERIFY(!QFile::exists(active)); QCOMPARE(settings.settings(), initial);
    }
    void previewPreservesLocalPathsAndImportIsExplicit() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); const QString path = directory.filePath("settings.json");
        QVERIFY(writeJson(path, initial)); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, path, keys, history); SettingsBackupDialog dialog(settings);
        auto imported = preferences(directory.filePath("foreign")); auto ocr = imported.value("ocr").toObject(); ocr["language"] = "en-US"; imported["ocr"] = ocr;
        const QString backup = directory.filePath("backup.json"); QVERIFY(saveSettingsBackup(backup, imported, nullptr));
        QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", backup));
        QVERIFY(preview(dialog)->toPlainText().contains(directory.filePath("foreign/history")));
        QVERIFY(preview(dialog)->toPlainText().contains("zh-Hans-CN → en-US")); QVERIFY(!directories(dialog)->isChecked());
        QCOMPARE(settings.settings(), initial); QSignalSpy restored(&dialog, &SettingsBackupDialog::settingsRestored);
        QVERIFY(confirm(dialog, "ImportSettingsBackup", QMessageBox::No).defaultNo); QCOMPARE(restored.count(), 0); QCOMPARE(readJson(path), initial);
        QVERIFY(confirm(dialog, "ImportSettingsBackup", QMessageBox::Yes).defaultNo);
        QVERIFY(settings.busy()); QVERIFY(!button(dialog, "CloseSettingsBackup")->isEnabled());
        QSignalSpy finished(&dialog, &QDialog::finished); dialog.reject(); QCOMPARE(finished.count(), 0);
        QTRY_COMPARE(restored.count(), 1); QTRY_VERIFY(!settings.busy());
        QCOMPARE(settings.settings().value("ocr"), imported.value("ocr"));
        for (const auto &group : {"save", "history", "clipboard"}) QCOMPARE(settings.settings().value(group).toObject().value("dir"), initial.value(group).toObject().value("dir"));
        QCOMPARE(settings.settings().value("future"), initial.value("future")); QCOMPARE(readJson(path), settings.settings());
        QCOMPARE(dialog.findChild<QLabel *>("SettingsBackupPath")->text(), backup); QVERIFY(button(dialog, "CloseSettingsBackup")->isEnabled());
        QVERIFY(status(dialog)->text().contains(t("已导入配置。")));
    }
    void parseAndApplyFailuresKeepTheChosenBackupAndOriginalBytes() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); const QString path = directory.filePath("settings.json");
        QVERIFY(writeJson(path, initial)); QFile active(path); QVERIFY(active.open(QIODevice::ReadOnly)); const auto bytes = active.readAll(); active.close();
        HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, path, keys, history); SettingsBackupDialog dialog(settings);
        auto imported = initial; auto shortcuts = imported.value("hotkeys").toObject(); shortcuts["capture"] = "Ctrl+F12"; imported["hotkeys"] = shortcuts;
        const QString backup = directory.filePath("backup.json"); QVERIFY(saveSettingsBackup(backup, imported, nullptr));
        QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", backup)); directories(dialog)->setChecked(true);
        const QString before = preview(dialog)->toPlainText(), bad = directory.filePath("bad.json");
        QFile malformed(bad); QVERIFY(malformed.open(QIODevice::WriteOnly)); malformed.write("not json"); malformed.close();
        QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", bad)); QCOMPARE(preview(dialog)->toPlainText(), before);
        QVERIFY(directories(dialog)->isChecked()); QCOMPARE(dialog.findChild<QLabel *>("SettingsBackupPath")->text(), backup);
        QVERIFY(status(dialog)->text().contains(bad)); QSignalSpy restored(&dialog, &SettingsBackupDialog::settingsRestored);
        confirm(dialog, "ImportSettingsBackup", QMessageBox::Yes);
        QCOMPARE(restored.count(), 0); QVERIFY(!settings.busy()); QCOMPARE(settings.settings(), initial);
        QVERIFY(status(dialog)->text().contains(t("操作未完成，原设置已保留。")));
        QCOMPARE(preview(dialog)->toPlainText(), before); QVERIFY(directories(dialog)->isChecked());
        QVERIFY(active.open(QIODevice::ReadOnly)); QCOMPARE(active.readAll(), bytes);
    }
    void directoryRestorationRequiresAnExplicitChoice() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); const QString path = directory.filePath("settings.json");
        QVERIFY(writeJson(path, initial)); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, path, keys, history); SettingsBackupDialog dialog(settings);
        auto imported = preferences(directory.filePath("restored")); const QString backup = directory.filePath("backup.json");
        QVERIFY(saveSettingsBackup(backup, imported, nullptr)); QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", backup));
        directories(dialog)->setChecked(true); QVERIFY(preview(dialog)->toPlainText().contains(t("将按备份恢复目录。")));
        QSignalSpy restored(&dialog, &SettingsBackupDialog::settingsRestored); confirm(dialog, "ImportSettingsBackup", QMessageBox::Yes);
        QTRY_COMPARE(restored.count(), 1);
        for (const auto &group : {"save", "history", "clipboard"}) QCOMPARE(settings.settings().value(group).toObject().value("dir"), imported.value(group).toObject().value("dir"));
        QCOMPARE(readJson(path), settings.settings());
    }
    void foreignDirectoryIsVisibleButCannotBeAppliedOnThisPlatform() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, directory.filePath("settings.json"), keys, history); SettingsBackupDialog dialog(settings);
        auto imported = initial; auto save = imported.value("save").toObject();
#ifdef Q_OS_WIN
        save["dir"] = "/home/other/Pictures";
#else
        save["dir"] = "C:\\Users\\Other\\Pictures";
#endif
        if (QDir::isAbsolutePath(save.value("dir").toString())) QSKIP("This platform also accepts the alternate platform's absolute path syntax");
        imported["save"] = save; const QString backup = directory.filePath("backup.json"); QVERIFY(saveSettingsBackup(backup, imported, nullptr));
        QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", backup)); directories(dialog)->setChecked(true);
        button(dialog, "ImportSettingsBackup")->click(); QVERIFY(!settings.busy()); QCOMPARE(settings.settings(), initial);
        QVERIFY(status(dialog)->text().contains(t("备份目录不适用于本机，请保留当前目录或重新选择路径。")));
    }
    void restoringDefaultsKeepsLocalDirectoriesAndRealDefaultKeys() {
        QTemporaryDir directory; auto initial = preferences(directory.path()); auto ocr = initial.value("ocr").toObject(); ocr["language"] = "en-US"; initial["ocr"] = ocr;
        const QString path = directory.filePath("settings.json"); QVERIFY(writeJson(path, initial)); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, path, keys, history); SettingsBackupDialog dialog(settings); QSignalSpy restored(&dialog, &SettingsBackupDialog::settingsRestored);
        QVERIFY(confirm(dialog, "RestoreDefaultSettings", QMessageBox::Yes).defaultNo); QTRY_COMPARE(restored.count(), 1);
        const auto expected = defaultSettings(); QCOMPARE(settings.settings().value("hotkeys"), expected.value("hotkeys")); QCOMPARE(settings.settings().value("ocr"), expected.value("ocr"));
        for (const auto &group : {"save", "history", "clipboard"}) QCOMPARE(settings.settings().value(group).toObject().value("dir"), initial.value(group).toObject().value("dir"));
        QCOMPARE(settings.settings().value("future"), initial.value("future")); QCOMPARE(readJson(path), settings.settings());
    }
    void waitingForAnotherSaveClearsWhenItFinishes() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, directory.filePath("settings.json"), keys, history);
        settings.apply({{"ocr", QJsonObject{{"language", "en-US"}}}}); QVERIFY(settings.busy()); SettingsBackupDialog dialog(settings);
        QVERIFY(!button(dialog, "ChooseSettingsBackup")->isEnabled()); QVERIFY(status(dialog)->text().contains(t("等待当前设置保存完成…")));
        QTRY_VERIFY(!settings.busy()); QVERIFY(button(dialog, "ChooseSettingsBackup")->isEnabled()); QVERIFY(status(dialog)->text().isEmpty());
    }
    void directoryChoiceOutlineStaysVisible_data() {
        QTest::addColumn<QString>("theme"); QTest::newRow("light") << QString("light"); QTest::newRow("dark") << QString("dark");
    }
    void directoryChoiceOutlineStaysVisible() {
        QFETCH(QString, theme); InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", theme}});
        QApplication::setPalette(QGuiApplication::palette());
        QTemporaryDir directory; const auto initial = preferences(directory.path()); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, directory.filePath("settings.json"), keys, history); SettingsBackupDialog dialog(settings);
        const QString backup = directory.filePath("backup.json"); QVERIFY(saveSettingsBackup(backup, initial, nullptr));
        dialog.show(); QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", backup));
        auto *choice = directories(dialog); QVERIFY(choice->isEnabled()); QVERIFY(!choice->isChecked());
        QStyleOptionButton option; option.initFrom(choice); option.rect = choice->rect(); option.state |= QStyle::State_Off;
        const QRect indicator = choice->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, choice); QVERIFY(!indicator.isEmpty());
        auto luminance = [](const QColor &color) {
            auto linear = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
            return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
        };
        auto contrast = [&](const QColor &first, const QColor &second) {
            const double a = luminance(first), b = luminance(second); return (std::max(a, b) + .05) / (std::min(a, b) + .05);
        };
        const QImage unchecked = choice->grab().toImage(); const qreal dpr = unchecked.devicePixelRatio();
        const QColor background = choice->palette().color(QPalette::Window);
        QCOMPARE(background.lightness() < 128, theme == "dark");
        for (const QPoint edge : {QPoint(indicator.left(), indicator.center().y()), QPoint(indicator.right(), indicator.center().y()),
                                  QPoint(indicator.center().x(), indicator.top()), QPoint(indicator.center().x(), indicator.bottom())}) {
            double best = 1;
            const QPoint pixel(qRound(edge.x() * dpr), qRound(edge.y() * dpr));
            for (int y = qMax(0, pixel.y() - 1); y <= qMin(unchecked.height() - 1, pixel.y() + 1); ++y)
                for (int x = qMax(0, pixel.x() - 1); x <= qMin(unchecked.width() - 1, pixel.x() + 1); ++x)
                    best = std::max(best, contrast(unchecked.pixelColor(x, y), background));
            QVERIFY2(best >= 3, qPrintable(QString("Unchecked indicator edge contrast %1 at %2,%3").arg(best).arg(edge.x()).arg(edge.y())));
        }
    }
    void languageAndThemeKeepTheChosenFileAndDirectoryChoice() {
        QTemporaryDir directory; const auto initial = preferences(directory.path()); HistoryStore history(initial.value("history").toObject()); Hotkeys keys;
        SettingsApply settings(initial, directory.filePath("settings.json"), keys, history); SettingsBackupDialog dialog(settings);
        const QString backup = directory.filePath(QStringLiteral("复制.json")); QVERIFY(saveSettingsBackup(backup, initial, nullptr));
        QVERIFY(chooseFile(dialog, "ChooseSettingsBackup", backup)); directories(dialog)->setChecked(true);
        InterfacePreferences::instance().apply({{"language", "en"}, {"theme", "dark"}});
        QCOMPARE(dialog.windowTitle(), t("配置备份与恢复")); QCOMPARE(dialog.findChild<QLabel *>("SettingsBackupPath")->text(), backup);
        QVERIFY(directories(dialog)->isChecked()); QVERIFY(preview(dialog)->toPlainText().contains(initial.value("history").toObject().value("dir").toString()));
        QCOMPARE(settings.settings(), initial);
    }
};

QTEST_MAIN(SettingsBackupDialogTest)
#include "settings_backup_dialog_test.moc"
