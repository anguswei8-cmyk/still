#include "capture/capture_presets.h"
#include "ui/i18n.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

using namespace still;

static RegionPreset preset(const QString &name = {}) {
    return {name, {"Display-1", "Maker", "Panel", "Serial-1"}, QRectF(10.125, 20.375, 300.625, 200.875)};
}

static void enterName(CapturePresets &dialog, const QString &name) {
    auto *input = dialog.findChild<QLineEdit *>("RegionPresetName");
    QTest::mouseClick(input, Qt::LeftButton); input->selectAll(); QTest::keyClicks(input, name);
}

static bool deleteWithAnswer(CapturePresets &dialog, QMessageBox::StandardButton answer) {
    bool protectedFocus = false;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
        auto *confirmation = dialog.findChild<QMessageBox *>("DeleteRegionPresetConfirmation");
        Q_ASSERT(confirmation);
        protectedFocus = confirmation->defaultButton() == confirmation->button(QMessageBox::No)
            && confirmation->textFormat() == Qt::PlainText;
        QTest::mouseClick(confirmation->button(answer), Qt::LeftButton);
    });
    timer.start(0);
    QTest::mouseClick(dialog.findChild<QPushButton *>("DeleteRegionPreset"), Qt::LeftButton);
    return protectedFocus;
}

class CapturePresetsTest : public QObject {
    Q_OBJECT
private slots:
    void init() { InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", "light"}}); }
    void savingNeedsAConfirmedRegionAndAName() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json");
        auto dialog = std::make_unique<CapturePresets>(path);
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        auto *name = dialog->findChild<QLineEdit *>("RegionPresetName");
        auto *save = dialog->findChild<QPushButton *>("SaveRegionPreset");
        auto *capture = dialog->findChild<QPushButton *>("CaptureRegionPreset");
        auto *remove = dialog->findChild<QPushButton *>("DeleteRegionPreset");
        auto *status = dialog->findChild<QLabel *>("RegionPresetStatus");
        QVERIFY(list); QVERIFY(name); QVERIFY(save); QVERIFY(capture); QVERIFY(remove); QVERIFY(status);
        QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose)); QVERIFY(dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QCOMPARE(dialog->windowModality(), Qt::NonModal); QVERIFY(!dialog->isModal());
        QCOMPARE(name->maxLength(), 80); QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(!status->text().isEmpty()); QCOMPARE(list->count(), 0);
        QVERIFY(!save->isEnabled()); QVERIFY(!capture->isEnabled()); QVERIFY(!remove->isEnabled());
        dialog->show(); enterName(*dialog, "Work"); QVERIFY(!save->isEnabled());
        dialog->setCurrentRegion(preset()); QVERIFY(save->isEnabled());
        enterName(*dialog, "   "); QVERIFY(!save->isEnabled());
        enterName(*dialog, QString(81, 'x')); QCOMPARE(name->text().size(), 80); QVERIFY(save->isEnabled());
        dialog->setCurrentRegion({}); QVERIFY(!save->isEnabled()); QVERIFY(!QFile::exists(path));
    }
    void savingUsesACopyAndKeepsOrderWithoutOverwritingDuplicateNames() {
        QTemporaryDir directory;
        const auto path = directory.filePath("nested/region-presets.json");
        auto dialog = std::make_unique<CapturePresets>(path);
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        auto *save = dialog->findChild<QPushButton *>("SaveRegionPreset");
        auto *status = dialog->findChild<QLabel *>("RegionPresetStatus");
        auto confirmed = preset("Ignored name"); const auto original = confirmed;
        dialog->setCurrentRegion(confirmed); confirmed.region.moveLeft(999);
        dialog->show(); enterName(*dialog, "  Work  "); QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Work")); QCOMPARE(list->currentRow(), 0);
        QVERIFY(dialog->findChild<QLineEdit *>("RegionPresetName")->text().isEmpty());
        QString error; auto loaded = loadRegionPresets(path, &error);
        QVERIFY2(loaded, qPrintable(error)); QCOMPARE(loaded->size(), 1); QCOMPARE((*loaded)[0].name, QString("Work"));
        QCOMPARE((*loaded)[0].region, original.region); QCOMPARE((*loaded)[0].screen.name, original.screen.name);
        QCOMPARE((*loaded)[0].screen.manufacturer, original.screen.manufacturer);
        QCOMPARE((*loaded)[0].screen.model, original.screen.model); QCOMPARE((*loaded)[0].screen.serialNumber, original.screen.serialNumber);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        dialog->setCurrentRegion(confirmed); const auto instruction = status->text();
        enterName(*dialog, "Work"); QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(list->count(), 1); QCOMPARE(list->currentRow(), 0); QVERIFY(status->text() != instruction);
        QCOMPARE(status->text(), t("名称已存在，请使用其他名称。"));
        QCOMPARE(dialog->findChild<QLineEdit *>("RegionPresetName")->text(), QString("Work"));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        enterName(*dialog, "work"); QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(list->count(), 2); QCOMPARE(list->item(1)->text(), QString("work")); QCOMPARE(list->currentRow(), 1);
        loaded = loadRegionPresets(path, &error); QVERIFY(loaded); QCOMPARE(loaded->size(), 2);
        QCOMPARE((*loaded)[0].name, QString("Work")); QCOMPARE((*loaded)[1].name, QString("work"));
        QCOMPARE((*loaded)[0].region, original.region); QCOMPARE((*loaded)[1].region, confirmed.region);
        dialog.reset(); dialog = std::make_unique<CapturePresets>(path);
        list = dialog->findChild<QListWidget *>("RegionPresetList");
        QCOMPARE(list->count(), 2); QCOMPARE(list->item(0)->text(), QString("Work")); QCOMPARE(list->item(1)->text(), QString("work"));
        QVERIFY(!dialog->findChild<QPushButton *>("SaveRegionPreset")->isEnabled());
        QVERIFY(dialog->findChild<QPushButton *>("CaptureRegionPreset")->isEnabled());
    }
    void capturingTheSelectionHidesTheWindowBeforeEmittingACopy() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        auto second = preset("Second"); second.region = QRectF(30.5, 40.25, 120.75, 80.5);
        QVERIFY2(saveRegionPresets(path, {preset("First"), second}, &error), qPrintable(error));
        auto dialog = std::make_unique<CapturePresets>(path);
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        QSignalSpy requested(dialog.get(), &CapturePresets::captureRequested);
        RegionPreset captured; bool hiddenWhenRequested = false;
        connect(dialog.get(), &CapturePresets::captureRequested, this, [&](const RegionPreset &value) {
            hiddenWhenRequested = !dialog->isVisible(); captured = value;
        });
        dialog->show();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(1)).center());
        QCOMPARE(list->currentRow(), 1);
        QTest::mouseClick(dialog->findChild<QPushButton *>("CaptureRegionPreset"), Qt::LeftButton);
        QCOMPARE(requested.count(), 1); QVERIFY(hiddenWhenRequested); QVERIFY(dialog->isHidden());
        QCOMPARE(captured.name, second.name); QCOMPARE(captured.region, second.region); QCOMPARE(captured.screen.serialNumber, second.screen.serialNumber);
        dialog->setCurrentRegion(preset("New current region")); QCOMPARE(captured.name, second.name); QCOMPARE(captured.region, second.region);
    }
    void enterSavesFromTheNameAndCapturesFromTheListOrFocusedButton_data() {
        QTest::addColumn<bool>("fromList");
        QTest::newRow("list") << true;
        QTest::newRow("focused-button") << false;
    }
    void enterSavesFromTheNameAndCapturesFromTheListOrFocusedButton() {
        QFETCH(bool, fromList);
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json");
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        auto *name = dialog->findChild<QLineEdit *>("RegionPresetName");
        auto *save = dialog->findChild<QPushButton *>("SaveRegionPreset");
        auto *capture = dialog->findChild<QPushButton *>("CaptureRegionPreset");
        const auto focusState = [&] {
            const auto *active = QApplication::activeWindow();
            const auto *focus = QApplication::focusWidget();
            return QString("visible=%1 active=%2 activeWindow=%3/%4 focus=%5/%6").arg(dialog->isVisible()).arg(dialog->isActiveWindow())
                .arg(active ? active->metaObject()->className() : "<none>", active ? active->objectName() : QString())
                .arg(focus ? focus->metaObject()->className() : "<none>", focus ? focus->objectName() : QString());
        };
        QSignalSpy requested(dialog.get(), &CapturePresets::captureRequested);
        enterName(*dialog, "Work"); QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Work")); QCOMPARE(requested.count(), 0);
        enterName(*dialog, "Another"); QVERIFY(save->isEnabled());
        dialog->activateWindow(); QTRY_VERIFY2(QApplication::activeWindow() == dialog.get(), qPrintable(focusState()));
        QWidget *target = fromList ? static_cast<QWidget *>(list) : capture;
        target->setFocus(Qt::TabFocusReason); QTRY_VERIFY2(target->hasFocus(), qPrintable(focusState()));
        QTest::keyClick(target, Qt::Key_Return);
        QCOMPARE(requested.count(), 1); QVERIFY(dialog->isHidden()); QCOMPARE(list->count(), 1); QCOMPARE(name->text(), QString("Another"));
        QString error; const auto loaded = loadRegionPresets(path, &error); QVERIFY(loaded); QCOMPARE(loaded->size(), 1); QCOMPARE((*loaded)[0].name, QString("Work"));
    }
    void enterInAnEmptyListDoesNotSaveThePendingName() {
        QTemporaryDir directory; const auto path = directory.filePath("region-presets.json");
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        auto *name = dialog->findChild<QLineEdit *>("RegionPresetName"); QSignalSpy requested(dialog.get(), &CapturePresets::captureRequested);
        enterName(*dialog, "Pending"); dialog->activateWindow(); QTRY_COMPARE(QApplication::activeWindow(), dialog.get());
        list->setFocus(); QTRY_VERIFY(list->hasFocus()); QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(requested.count(), 0); QCOMPARE(list->count(), 0); QCOMPARE(name->text(), QString("Pending")); QVERIFY(!QFile::exists(path)); QVERIFY(dialog->isVisible());
    }
    void deletingRequiresConfirmationAndPersistsOnlyAfterYes() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY2(saveRegionPresets(path, {preset("<b>First</b>"), preset("Second")}, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        auto dialog = std::make_unique<CapturePresets>(path);
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList"); dialog->show();
        QVERIFY(deleteWithAnswer(*dialog, QMessageBox::No));
        QCOMPARE(list->count(), 2); QCOMPARE(list->item(0)->text(), QString("<b>First</b>")); QCOMPARE(list->currentRow(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        QVERIFY(deleteWithAnswer(*dialog, QMessageBox::Yes));
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Second")); QCOMPARE(list->currentRow(), 0);
        auto loaded = loadRegionPresets(path, &error); QVERIFY(loaded); QCOMPARE(loaded->size(), 1); QCOMPARE((*loaded)[0].name, QString("Second"));
        QVERIFY(deleteWithAnswer(*dialog, QMessageBox::Yes)); QCOMPARE(list->count(), 0);
        QVERIFY(!dialog->findChild<QPushButton *>("CaptureRegionPreset")->isEnabled());
        QVERIFY(!dialog->findChild<QPushButton *>("DeleteRegionPreset")->isEnabled());
        loaded = loadRegionPresets(path, &error); QVERIFY(loaded); QVERIFY(loaded->isEmpty());
    }
    void aDamagedFileDisablesActionsAndKeepsItsBytes() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); const QByteArray damaged("{\"version\":12,\"presets\":[]}");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(damaged), damaged.size()); file.close();
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        auto *status = dialog->findChild<QLabel *>("RegionPresetStatus");
        QVERIFY(!list->isEnabled()); QCOMPARE(list->count(), 0); QVERIFY(!status->text().isEmpty()); QCOMPARE(status->textFormat(), Qt::PlainText);
        QSignalSpy requested(dialog.get(), &CapturePresets::captureRequested);
        for (const char *name : {"SaveRegionPreset", "CaptureRegionPreset", "DeleteRegionPreset"}) {
            auto *button = dialog->findChild<QPushButton *>(name); QVERIFY(!button->isEnabled()); QTest::mouseClick(button, Qt::LeftButton);
        }
        QCOMPARE(requested.count(), 0); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), damaged);
    }
    void badFileMessageFollowsLanguageButKeepsPath() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); const QByteArray damaged("{\"version\":99,\"presets\":[]}");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(damaged), damaged.size()); file.close();
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        auto *status = dialog->findChild<QLabel *>("RegionPresetStatus");
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        dialog->findChild<QLineEdit *>("RegionPresetName")->setText("Never write");
        QSignalSpy requested(dialog.get(), &CapturePresets::captureRequested);
        for (const auto &language : {"zh_CN", "en", "zh_CN"}) {
            InterfacePreferences::instance().apply({{"language", language}, {"theme", "light"}});
            const auto prefix = QString(language) == "en" ? QStringLiteral("The region presets file is damaged or unsupported")
                : QStringLiteral("区域预设文件损坏或版本不支持");
            QVERIFY2(status->text().startsWith(prefix), qPrintable(status->text()));
            QVERIFY(status->text().contains(path)); QCOMPARE(status->textFormat(), Qt::PlainText);
            QVERIFY(!list->isEnabled()); QCOMPARE(list->count(), 0);
            for (const char *name : {"SaveRegionPreset", "CaptureRegionPreset", "DeleteRegionPreset"}) {
                auto *button = dialog->findChild<QPushButton *>(name); QVERIFY(!button->isEnabled()); QTest::mouseClick(button, Qt::LeftButton);
            }
            QCOMPARE(requested.count(), 0); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), damaged); file.close();
        }
    }
    void anExternallyDamagedFileCannotBeOverwrittenByAnOpenDialog() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY2(saveRegionPresets(path, {preset("Keep")}, &error), qPrintable(error));
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("damaged"), 7); file.close();
        enterName(*dialog, "New"); QTest::mouseClick(dialog->findChild<QPushButton *>("SaveRegionPreset"), Qt::LeftButton);
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Keep")); QCOMPARE(list->currentRow(), 0);
        QVERIFY(dialog->findChild<QLabel *>("RegionPresetStatus")->text().contains(path));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("damaged"));
    }
    void failedWritesKeepTheVisibleListAndSelection() {
        QTemporaryDir directory;
        const auto parent = directory.filePath("occupied"), path = QDir(parent).filePath("region-presets.json"); QString error;
        QVERIFY2(saveRegionPresets(path, {preset("Keep")}, &error), qPrintable(error));
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        QVERIFY(QFile::remove(path)); QVERIFY(QDir().rmdir(parent));
        QFile file(parent); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("keep parent"), 11); file.close();
        enterName(*dialog, "New"); const auto instruction = dialog->findChild<QLabel *>("RegionPresetStatus")->text();
        QTest::mouseClick(dialog->findChild<QPushButton *>("SaveRegionPreset"), Qt::LeftButton);
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Keep")); QCOMPARE(list->currentRow(), 0);
        QVERIFY(dialog->findChild<QLabel *>("RegionPresetStatus")->text() != instruction);
        QCOMPARE(dialog->findChild<QLineEdit *>("RegionPresetName")->text(), QString("New"));
        QVERIFY(deleteWithAnswer(*dialog, QMessageBox::Yes));
        QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Keep")); QCOMPARE(list->currentRow(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep parent"));
    }
    void languageAndThemeChangesKeepUserNames() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY2(saveRegionPresets(path, {preset("Cancel")}, &error), qPrintable(error));
        auto dialog = std::make_unique<CapturePresets>(path); dialog->setCurrentRegion(preset()); dialog->show();
        auto *list = dialog->findChild<QListWidget *>("RegionPresetList");
        auto *name = dialog->findChild<QLineEdit *>("RegionPresetName");
        enterName(*dialog, "Close");
        auto &preferences = InterfacePreferences::instance(); preferences.apply({{"language", "en"}, {"theme", "dark"}});
        QCOMPARE(dialog->windowTitle(), t("区域预设")); QVERIFY(dialog->windowTitle() != QStringLiteral("区域预设"));
        QCOMPARE(dialog->findChild<QPushButton *>("SaveRegionPreset")->text(), t("保存上次区域"));
        QCOMPARE(dialog->findChild<QPushButton *>("CaptureRegionPreset")->accessibleName(), t("截取所选"));
        QCOMPARE(name->accessibleName(), t("预设名称")); QCOMPARE(list->accessibleName(), t("区域预设列表"));
        QCOMPARE(list->item(0)->text(), QString("Cancel")); QCOMPARE(name->text(), QString("Close")); QCOMPARE(list->currentRow(), 0);
        QTRY_COMPARE(dialog->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
        preferences.apply({{"language", "zh_CN"}, {"theme", "light"}});
        QCOMPARE(dialog->windowTitle(), QStringLiteral("区域预设"));
        QCOMPARE(dialog->findChild<QPushButton *>("SaveRegionPreset")->text(), QStringLiteral("保存上次区域"));
        QCOMPARE(list->item(0)->text(), QString("Cancel")); QCOMPARE(name->text(), QString("Close"));
        QTRY_COMPARE(dialog->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
    }
    void closingDeletesTheWindowWithoutChangingTheFile_data() {
        QTest::addColumn<QString>("method");
        QTest::newRow("close-button") << QString("button");
        QTest::newRow("escape") << QString("escape");
        QTest::newRow("window-close") << QString("close");
    }
    void closingDeletesTheWindowWithoutChangingTheFile() {
        QFETCH(QString, method);
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY2(saveRegionPresets(path, {preset("Keep")}, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        QPointer<CapturePresets> dialog = new CapturePresets(path);
        const auto cleanup = qScopeGuard([&] { delete dialog.data(); });
        QSignalSpy requested(dialog, &CapturePresets::captureRequested); dialog->show();
        if (method == "button") QTest::mouseClick(dialog->findChild<QPushButton *>("CloseRegionPresets"), Qt::LeftButton);
        else if (method == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
        else dialog->close();
        QTRY_VERIFY(!dialog); QCOMPARE(requested.count(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
    }
};

QTEST_MAIN(CapturePresetsTest)
#include "capture_presets_test.moc"
