#include "app/instance_request.h"

#include <QBuffer>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

using namespace still;

class InstanceRequestTest : public QObject {
    Q_OBJECT
private slots:
    void relativeImageUsesTheSendingDirectory() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        const QString image = QStringLiteral("截图 😀\nimage.png");
        QByteArray bytes = encodeInstanceRequest(image, true);
        QCOMPARE(bytes.count('\n'), 1);
        QBuffer input(&bytes); QVERIFY(input.open(QIODevice::ReadOnly));
        const auto request = readInstanceRequest(input); QVERIFY(request);
        QCOMPARE(request->image, QDir::current().absoluteFilePath(image)); QVERIFY(request->capture);
    }
    void emptyRequestShowsTheExistingWindow() {
        QByteArray bytes = encodeInstanceRequest({}, false);
        QBuffer input(&bytes); QVERIFY(input.open(QIODevice::ReadOnly));
        const auto request = readInstanceRequest(input); QVERIFY(request);
        QVERIFY(request->image.isEmpty()); QVERIFY(!request->capture);
    }
    void fragmentedRequestWaitsForItsCompleteLine() {
        const auto message = encodeInstanceRequest({}, true);
        QByteArray bytes;
        QBuffer input(&bytes); QVERIFY(input.open(QIODevice::ReadOnly));
        for (int i = 0; i < message.size() - 1; ++i) {
            bytes += message[i];
            QVERIFY(!readInstanceRequest(input)); QCOMPARE(input.pos(), 0);
        }
        bytes += '\n';
        const auto request = readInstanceRequest(input); QVERIFY(request);
        QVERIFY(request->capture); QVERIFY(request->image.isEmpty());
        QVERIFY(!readInstanceRequest(input));
    }
    void invalidMessagesDoNotBecomeShowRequests_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("not-json") << QByteArray("show\n");
        QTest::newRow("array") << QByteArray("[]\n");
        QTest::newRow("missing-fields") << QByteArray("{}\n");
        QTest::newRow("wrong-image-type") << QByteArray("{\"image\":true,\"capture\":false}\n");
        QTest::newRow("wrong-capture-type") << QByteArray("{\"image\":\"\",\"capture\":\"true\"}\n");
    }
    void invalidMessagesDoNotBecomeShowRequests() {
        QFETCH(QByteArray, bytes);
        QBuffer input(&bytes); QVERIFY(input.open(QIODevice::ReadOnly));
        QVERIFY(!readInstanceRequest(input)); QVERIFY(input.atEnd());
    }
};

QTEST_GUILESS_MAIN(InstanceRequestTest)
#include "instance_request_test.moc"
