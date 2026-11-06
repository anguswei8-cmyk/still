#include "capture/capture_delay.h"
#include "ui/i18n.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimer>

using namespace still;

class CaptureDelayTest : public QObject {
    Q_OBJECT
private slots:
    void init() { InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", "light"}}); }
    void countdownWaitsForItsDeadlineWithoutBlockingEvents() {
        QPointer<CaptureDelay> dialog = new CaptureDelay;
        const auto cleanup = qScopeGuard([&] { if (dialog) dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        auto *seconds = dialog->findChild<QSpinBox *>("DelaySeconds");
        auto *inputs = dialog->findChild<QWidget *>("DelayInputs");
        auto *start = dialog->findChild<QPushButton *>("DelayStart");
        auto *cancel = dialog->findChild<QPushButton *>("DelayCancel");
        auto *countdown = dialog->findChild<QLabel *>("DelayCountdown");
        QVERIFY(seconds); QVERIFY(inputs); QVERIFY(start); QVERIFY(cancel); QVERIFY(countdown);
        QCOMPARE(seconds->minimum(), 1); QCOMPARE(seconds->maximum(), 60); QCOMPARE(seconds->value(), 5);
        QVERIFY(countdown->isHidden()); QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose));
        QCOMPARE(dialog->windowModality(), Qt::NonModal); QVERIFY(!dialog->isModal());
        QSignalSpy accepted(dialog, &QDialog::accepted), rejected(dialog, &QDialog::rejected), finished(dialog, &QDialog::finished);
        QSignalSpy started(start, &QPushButton::clicked);
        QElapsedTimer elapsed; qint64 acceptedAt = -1;
        connect(dialog, &QDialog::accepted, this, [&] { acceptedAt = elapsed.elapsed(); });
        bool otherEventRan = false; QTimer otherEvent;
        otherEvent.setSingleShot(true); connect(&otherEvent, &QTimer::timeout, this, [&] { otherEventRan = true; });
        dialog->show(); QVERIFY(inputs->isVisible()); QVERIFY(start->isVisible());
        seconds->setValue(1); elapsed.start(); otherEvent.start(50);
        QTest::mouseClick(start, Qt::LeftButton);
        QVERIFY(dialog); QVERIFY(inputs->isHidden()); QVERIFY(start->isHidden()); QVERIFY(countdown->isVisible());
        QVERIFY(!dialog->isModal());
        QCOMPARE(countdown->text(), t("%1 秒后开始截图").arg(1)); QCOMPARE(started.count(), 1);
        QTest::keyClick(dialog, Qt::Key_Return);
        QCOMPARE(started.count(), 1); QCOMPARE(accepted.count(), 0); QCOMPARE(rejected.count(), 0);
        QTest::qWait(250);
        QVERIFY(otherEventRan); QCOMPARE(accepted.count(), 0); QCOMPARE(finished.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 1500);
        QVERIFY(acceptedAt >= 1000); QCOMPARE(rejected.count(), 0);
        QCOMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toInt(), int(QDialog::Accepted));
        QTRY_VERIFY(!dialog); QCOMPARE(accepted.count(), 1); QCOMPARE(finished.count(), 1);
    }
    void cancellingRunningCountdownNeverAccepts_data() {
        QTest::addColumn<QString>("method");
        QTest::newRow("cancel-button") << QString("cancel");
        QTest::newRow("escape") << QString("escape");
        QTest::newRow("window-close") << QString("close");
    }
    void cancellingRunningCountdownNeverAccepts() {
        QFETCH(QString, method);
        QPointer<CaptureDelay> dialog = new CaptureDelay;
        const auto cleanup = qScopeGuard([&] { if (dialog) dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        auto *seconds = dialog->findChild<QSpinBox *>("DelaySeconds");
        auto *start = dialog->findChild<QPushButton *>("DelayStart");
        auto *cancel = dialog->findChild<QPushButton *>("DelayCancel");
        QVERIFY(seconds); QVERIFY(start); QVERIFY(cancel);
        QSignalSpy accepted(dialog, &QDialog::accepted), rejected(dialog, &QDialog::rejected), finished(dialog, &QDialog::finished);
        dialog->show(); seconds->setValue(1); QTest::mouseClick(start, Qt::LeftButton);
        QTest::qWait(50); QVERIFY(dialog); QCOMPARE(accepted.count(), 0);
        if (method == "cancel") QTest::mouseClick(cancel, Qt::LeftButton);
        else if (method == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
        else dialog->close();
        QTRY_COMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toInt(), int(QDialog::Rejected));
        QCOMPARE(rejected.count(), 1); QTRY_VERIFY(!dialog);
        QTest::qWait(1150);
        QCOMPARE(accepted.count(), 0); QCOMPARE(rejected.count(), 1); QCOMPARE(finished.count(), 1);
    }
    void cancellingBeforeStartDoesNotAccept() {
        QPointer<CaptureDelay> dialog = new CaptureDelay;
        const auto cleanup = qScopeGuard([&] { if (dialog) dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        auto *cancel = dialog->findChild<QPushButton *>("DelayCancel"); QVERIFY(cancel);
        QSignalSpy accepted(dialog, &QDialog::accepted), rejected(dialog, &QDialog::rejected), finished(dialog, &QDialog::finished);
        dialog->show(); QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(accepted.count(), 0); QCOMPARE(rejected.count(), 1);
        QCOMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toInt(), int(QDialog::Rejected));
        QTRY_VERIFY(!dialog);
    }
    void overdueDeadlineFinishesOnTheFirstEventLoopTurn() {
        QPointer<CaptureDelay> dialog = new CaptureDelay;
        const auto cleanup = qScopeGuard([&] { if (dialog) dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        auto *seconds = dialog->findChild<QSpinBox *>("DelaySeconds");
        auto *start = dialog->findChild<QPushButton *>("DelayStart"); QVERIFY(seconds); QVERIFY(start);
        QSignalSpy accepted(dialog, &QDialog::accepted), rejected(dialog, &QDialog::rejected), finished(dialog, &QDialog::finished);
        dialog->show(); seconds->setValue(1); QTest::mouseClick(start, Qt::LeftButton);
        QTest::qSleep(1150); QCOMPARE(accepted.count(), 0);
        QCoreApplication::processEvents();
        QCOMPARE(accepted.count(), 1); QCOMPARE(rejected.count(), 0);
        QCOMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toInt(), int(QDialog::Accepted));
        QTRY_VERIFY(!dialog);
        QTest::qWait(150); QCOMPARE(accepted.count(), 1); QCOMPARE(finished.count(), 1);
    }
    void runningCountdownFollowsLanguageAndThemeChanges() {
        QPointer<CaptureDelay> dialog = new CaptureDelay;
        const auto cleanup = qScopeGuard([&] { if (dialog) dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        auto *seconds = dialog->findChild<QSpinBox *>("DelaySeconds");
        auto *inputs = dialog->findChild<QWidget *>("DelayInputs");
        auto *start = dialog->findChild<QPushButton *>("DelayStart");
        auto *cancel = dialog->findChild<QPushButton *>("DelayCancel");
        auto *countdown = dialog->findChild<QLabel *>("DelayCountdown");
        QVERIFY(seconds); QVERIFY(inputs); QVERIFY(start); QVERIFY(cancel); QVERIFY(countdown);
        auto *label = inputs->findChild<QLabel *>(); QVERIFY(label);
        QSignalSpy accepted(dialog, &QDialog::accepted), finished(dialog, &QDialog::finished);
        auto &preferences = InterfacePreferences::instance(); QSignalSpy changed(&preferences, &InterfacePreferences::changed);
        dialog->show(); seconds->setValue(60); QTest::mouseClick(start, Qt::LeftButton);
        QCOMPARE(countdown->text(), QStringLiteral("60 秒后开始截图"));
        preferences.apply({{"language", "en"}, {"theme", "dark"}});
        QCOMPARE(changed.count(), 1); QVERIFY(preferences.english()); QVERIFY(preferences.dark());
        QCOMPARE(dialog->windowTitle(), t("延时截图")); QVERIFY(dialog->windowTitle() != QStringLiteral("延时截图"));
        QCOMPARE(start->text(), t("开始倒计时")); QVERIFY(start->text() != QStringLiteral("开始倒计时"));
        QCOMPARE(cancel->text(), t("取消")); QCOMPARE(label->text(), t("延时（秒）："));
        QCOMPARE(seconds->accessibleName(), t("延时秒数"));
        const int englishSeconds = QRegularExpression("[0-9]+").match(countdown->text()).captured().toInt();
        QVERIFY(englishSeconds > 0 && englishSeconds <= 60);
        QCOMPARE(countdown->text(), t("%1 秒后开始截图").arg(englishSeconds));
        QVERIFY(countdown->text() != QStringLiteral("%1 秒后开始截图").arg(englishSeconds));
        QTRY_COMPARE(dialog->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
        preferences.apply({{"language", "zh_CN"}, {"theme", "light"}});
        QCOMPARE(changed.count(), 2); QCOMPARE(dialog->windowTitle(), QStringLiteral("延时截图"));
        QCOMPARE(start->text(), QStringLiteral("开始倒计时")); QCOMPARE(cancel->text(), QStringLiteral("取消"));
        const int chineseSeconds = QRegularExpression("[0-9]+").match(countdown->text()).captured().toInt();
        QVERIFY(chineseSeconds > 0 && chineseSeconds <= englishSeconds);
        QCOMPARE(countdown->text(), QStringLiteral("%1 秒后开始截图").arg(chineseSeconds));
        QTRY_COMPARE(dialog->palette().color(QPalette::Window), QApplication::palette().color(QPalette::Window));
        QTest::mouseClick(cancel, Qt::LeftButton); QCOMPARE(accepted.count(), 0);
        QCOMPARE(finished.count(), 1); QCOMPARE(finished.first().first().toInt(), int(QDialog::Rejected)); QTRY_VERIFY(!dialog);
    }
};

QTEST_MAIN(CaptureDelayTest)
#include "capture_delay_test.moc"
