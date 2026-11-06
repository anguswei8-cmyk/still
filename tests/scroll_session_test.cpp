#include "capture/scroll_session.h"

#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThreadPool>
#include <QtConcurrentRun>

using namespace still;

static QImage page() {
    QImage image(60, 600, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
        image.setPixel(x, y, qRgb((y * 31 + x * 7) % 256, (y * 17 + x * 3) % 256, (y * 53 + x * 11) % 256));
    return image;
}

class ScrollSessionTest : public QObject {
    Q_OBJECT
private slots:
    void unchangedFramesWaitForUserWithoutConsumingLimit() {
        const auto first = page().copy(0, 0, 60, 100);
        int captures = 0;
        ScrollSession session([&](const QRect &) { ++captures; return first; });
        QSignalSpy finished(&session, &ScrollSession::finished), progress(&session, &ScrollSession::progress);
        QVERIFY(session.start(first.rect(), first, {200, 1000, 2}));
        QTest::qWait(500);
        QVERIFY(captures >= 2); QVERIFY(session.active());
        QCOMPARE(finished.count(), 0); QCOMPARE(progress.count(), 0);
        session.stop(); QCOMPARE(finished.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(finished[0][0]), first);
    }
    void transientUnmatchedFrameDoesNotEndCapture() {
        const auto source = page();
        QImage transition(60, 100, QImage::Format_ARGB32); transition.fill(Qt::gray);
        int captures = 0;
        ScrollSession session([&](const QRect &) { return ++captures == 1 ? transition : source.copy(0, 40, 60, 100); });
        QSignalSpy finished(&session, &ScrollSession::finished);
        QVERIFY(session.start(QRect(0, 0, 60, 100), source.copy(0, 0, 60, 100), {200, 1000, 2}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QVERIFY(captures >= 2);
        QCOMPARE(qvariant_cast<QImage>(finished[0][0]), source.copy(0, 0, 60, 140));
        QVERIFY(finished[0][2].toString().isEmpty());
    }
    void frameLimitDeliversLastSuccessfulImageOnce() {
        const auto source = page();
        const QRect region(10, 20, 60, 100);
        ScrollSession session([&](const QRect &requested) { return requested == region ? source.copy(0, 40, 60, 100) : QImage(); });
        QSignalSpy finished(&session, &ScrollSession::finished);
        QSignalSpy progress(&session, &ScrollSession::progress);
        QVERIFY(session.start(region, source.copy(0, 0, 60, 100), {200, 1000, 2}));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(finished[0][0]), source.copy(0, 0, 60, 140));
        QCOMPARE(finished[0][1].toRect(), region);
        QCOMPARE(progress.count(), 1);
        QVERIFY(!session.active());
        session.stop();
        QCOMPARE(finished.count(), 1);
    }
    void stopDuringJoinCannotChangeNextSessionOrQueueMoreFrames() {
        auto *pool = QThreadPool::globalInstance();
        const int previousMaximum = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore entered, release;
        auto blocker = QtConcurrent::run(pool, [&] { entered.release(); release.acquire(); });
        const auto unblock = qScopeGuard([&] { release.release(); pool->waitForDone(); pool->setMaxThreadCount(previousMaximum); });
        QVERIFY(entered.tryAcquire(1, 1000));
        const auto source = page();
        int round = 0, captures = 0;
        ScrollSession session([&](const QRect &) { ++captures; return source.copy(0, round ? 240 : 40, 60, 100); });
        QSignalSpy finished(&session, &ScrollSession::finished);
        QSignalSpy progress(&session, &ScrollSession::progress);
        const QRect region(0, 0, 60, 100);
        QVERIFY(session.start(region, source.copy(0, 0, 60, 100), {200, 1000, 2}));
        QTRY_COMPARE(captures, 1);
        session.stop();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(finished[0][0]), source.copy(0, 0, 60, 100));
        round = 1;
        QVERIFY(session.start(region, source.copy(0, 200, 60, 100), {200, 1000, 2}));
        QTest::qWait(240);
        QCOMPARE(captures, 1);
        release.release();
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(qvariant_cast<QImage>(finished[1][0]), source.copy(0, 200, 60, 140));
        QCOMPARE(progress.count(), 1);
        QCOMPARE(captures, 2);
        QVERIFY(!session.active());
    }
    void captureFailureAndBudgetKeepLastGoodImage() {
        const auto first = page().copy(0, 0, 60, 100);
        ScrollSession session([](const QRect &) { return QImage(); });
        QSignalSpy finished(&session, &ScrollSession::finished);
        QVERIFY(!session.start(first.rect(), first, {200, 99, 2}));
        QVERIFY(!session.start(first.rect(), first, {200, 1000, 2, first.sizeInBytes() - 1}));
        QCOMPARE(finished.count(), 0);
        QVERIFY(session.start(first.rect(), first, {200, 1000, 2}));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(finished[0][0]), first);
        QVERIFY(!finished[0][2].toString().isEmpty());
        QVERIFY(!session.active());
    }
    void failedJoinDeliversOriginalAndRejectsSecondStartWhileActive() {
        const auto source = page();
        ScrollSession session([&](const QRect &) { return source.copy(0, 40, 60, 100); });
        QSignalSpy finished(&session, &ScrollSession::finished);
        const auto first = source.copy(0, 0, 60, 100);
        QVERIFY(session.start(first.rect(), first, {200, 120, 2}));
        QVERIFY(!session.start(first.rect(), first));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(qvariant_cast<QImage>(finished[0][0]), first);
        QVERIFY(!finished[0][2].toString().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ScrollSessionTest)
#include "scroll_session_test.moc"
