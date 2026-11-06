#include "imaging/api_proofreader.h"
#include "api_test_server.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <memory>

using namespace still;

namespace {
QByteArray completion(const QString &text = QStringLiteral("校对结果\r\nKeep  spaces")) {
    return QJsonDocument(QJsonObject{
        {"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"role", "assistant"}, {"content", text}}}}}},
        {"usage", QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 8}, {"total_tokens", 20}}}
    }).toJson(QJsonDocument::Compact);
}

using Service = ApiTestServer;

ApiProofreadResult result(const QSignalSpy &spy) { return qvariant_cast<ApiProofreadResult>(spy.last().first()); }
QJsonObject requestBody(const QByteArray &request) { return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object(); }
} // namespace

class ApiProofreaderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy); }
    void sendsOnlyExactSnapshotAndReturnsUsage() {
        Service service; service.body = completion(); QVERIFY(service.server.isListening());
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        const auto text = QStringLiteral("原始  文字\r\n\temoji😀\u2029尾部 ");
        client.start(service.endpoint(), "specific-model", "fixture-key", text);
        QVERIFY(client.busy()); QTRY_COMPARE(finished.count(), 1); QVERIFY(!client.busy());
        QCOMPARE(service.requests.size(), 1);
        const auto request = service.requests.first();
        QVERIFY(request.startsWith("POST /v1/chat/completions HTTP/1.1\r\n"));
        QVERIFY(request.contains("Authorization: Bearer fixture-key\r\n"));
        const auto json = requestBody(request);
        QCOMPARE(json.value("model").toString(), QString("specific-model"));
        QCOMPARE(json.value("stream"), QJsonValue(false)); QCOMPARE(json.value("store"), QJsonValue(false));
        QCOMPARE(json.size(), 4);
        const auto messages = json.value("messages").toArray(); QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].toObject().value("role").toString(), QString("system"));
        QCOMPARE(messages[1].toObject(), QJsonObject({{"role", "user"}, {"content", text}}));
        const auto answer = result(finished); QVERIFY(answer.error.isEmpty()); QVERIFY(!answer.canceled);
        QCOMPARE(answer.text, QStringLiteral("校对结果\r\nKeep  spaces"));
        QCOMPARE(answer.inputTokens, qint64(12)); QCOMPARE(answer.outputTokens, qint64(8)); QCOMPARE(answer.totalTokens, qint64(20));
    }
    void localServiceCanOmitKeyAndUsage() {
        Service service; auto json = QJsonDocument::fromJson(service.body).object(); json.remove("usage"); service.body = QJsonDocument(json).toJson();
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(service.endpoint(), "local-model", {}, "snapshot"); QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!service.requests.first().contains("Authorization:"));
        const auto answer = result(finished); QVERIFY(answer.error.isEmpty()); QCOMPARE(answer.inputTokens, qint64(-1)); QCOMPARE(answer.outputTokens, qint64(-1)); QCOMPARE(answer.totalTokens, qint64(-1));
    }
    void untrustedConfiguration_data() {
        QTest::addColumn<QUrl>("endpoint"); QTest::addColumn<QString>("model"); QTest::addColumn<QByteArray>("key"); QTest::addColumn<QString>("text");
        const QUrl good("https://example.invalid/v1/chat/completions");
        const auto row = [&](const char *name, const QUrl &url, const QString &model = "model", const QByteArray &key = "fixture-key", const QString &text = "snapshot") { QTest::newRow(name) << url << model << key << text; };
        row("relative", QUrl("v1/chat/completions")); row("missing-path", QUrl("https://example.invalid"));
        row("address-limit", QUrl(QString("https://example.invalid/%1").arg(QString(4096, 'x'))));
        row("http-remote", QUrl("http://example.invalid/v1/chat/completions"));
        row("userinfo", QUrl("https://user:password@example.invalid/v1/chat/completions"));
        row("empty-userinfo", QUrl("https://@example.invalid/v1/chat/completions"));
        row("query", QUrl("https://example.invalid/v1/chat/completions?api_key=secret"));
        row("empty-query", QUrl("https://example.invalid/v1/chat/completions?"));
        row("fragment", QUrl("https://example.invalid/v1/chat/completions#secret"));
        row("empty-model", good, " \t"); row("missing-remote-key", good, "model", {});
        row("model-limit", good, QString(1025, 'x'));
        row("newline-key", good, "model", "secret\r\nInjected: header"); row("nul-key", good, "model", QByteArray("secret\0value", 12));
        row("space-key", good, "model", "secret value"); row("non-ascii-key", good, "model", QByteArray::fromHex("c3a9"));
        row("key-store-limit", good, "model", QByteArray(2561, 'x'));
        row("empty-text", good, "model", "key", {}); row("blank-text", good, "model", "key", " \r\n\t");
        row("input-limit", good, "model", "key", QString(65537, 'x'));
    }
    void untrustedConfiguration() {
        QFETCH(QUrl, endpoint); QFETCH(QString, model); QFETCH(QByteArray, key); QFETCH(QString, text);
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        QVERIFY(!ApiProofreader::validationError(endpoint, model, key, text).isEmpty());
        client.start(endpoint, model, key, text); QCOMPARE(finished.count(), 1); QVERIFY(!client.busy());
        const auto answer = result(finished); QVERIFY(!answer.error.isEmpty()); QVERIFY(answer.text.isEmpty()); QVERIFY(!answer.error.contains("secret"));
    }
    void acceptsLoopbackAddressesWithoutKey_data() {
        QTest::addColumn<QUrl>("endpoint");
        for (const auto *host : {"localhost", "127.0.0.1", "127.255.0.1", "[::1]"})
            QTest::newRow(host) << QUrl(QString("http://%1/v1/chat/completions").arg(host));
        QTest::newRow("https-local") << QUrl("https://localhost/v1/chat/completions");
    }
    void acceptsLoopbackAddressesWithoutKey() {
        QFETCH(QUrl, endpoint);
        QVERIFY(ApiProofreader::validationError(endpoint, "model", {}, "snapshot").isEmpty());
    }
    void rejectsInvalidResponses_data() {
        QTest::addColumn<QByteArray>("body");
        const auto row = [](const char *name, const QJsonObject &root) { QTest::newRow(name) << QJsonDocument(root).toJson(); };
        QTest::newRow("malformed-json") << QByteArray("not JSON fixture-key snapshot");
        QTest::newRow("array-root") << QByteArray("[]");
        row("missing-choices", {}); row("empty-choices", {{"choices", QJsonArray{}}});
        const auto good = QJsonDocument::fromJson(completion()).object();
        for (const auto *reason : {"length", "content_filter", "tool_calls", "function_call", "unknown", ""}) {
            auto json = good; auto choice = json.value("choices").toArray().first().toObject(); choice["finish_reason"] = reason; json["choices"] = QJsonArray{choice}; row(reason, json);
        }
        const auto badMessage = [&](const char *name, const QJsonObject &message) {
            auto json = good; auto choice = json.value("choices").toArray().first().toObject(); choice["message"] = message; json["choices"] = QJsonArray{choice}; row(name, json);
        };
        badMessage("empty-text", {{"role", "assistant"}, {"content", ""}});
        badMessage("blank-text", {{"role", "assistant"}, {"content", " \r\n\t"}});
        badMessage("null-text", {{"role", "assistant"}, {"content", QJsonValue::Null}});
        badMessage("array-text", {{"role", "assistant"}, {"content", QJsonArray{"snapshot"}}});
        badMessage("wrong-role", {{"role", "user"}, {"content", "snapshot"}});
        badMessage("refused", {{"role", "assistant"}, {"content", "snapshot"}, {"refusal", "fixture-key private response"}});
        int usageCase = 0;
        for (const auto &value : {QJsonValue(-1), QJsonValue(1.5), QJsonValue(1e30), QJsonValue("123"), QJsonValue(QJsonValue::Null)}) {
            auto json = good; json["usage"] = QJsonObject{{"total_tokens", value}};
            const auto name = QString("invalid-usage-%1").arg(++usageCase); row(qPrintable(name), json);
        }
        auto json = good; json["usage"] = QJsonArray{1, 2, 3}; row("invalid-usage-object", json);
    }
    void rejectsInvalidResponses() {
        QFETCH(QByteArray, body); Service service; service.body = body;
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(service.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(finished.count(), 1);
        const auto answer = result(finished); QVERIFY(!answer.error.isEmpty()); QVERIFY(answer.text.isEmpty()); QVERIFY(!answer.error.contains("fixture-key")); QVERIFY(!answer.error.contains("snapshot"));
        QCOMPARE(service.requests.size(), 1); QVERIFY(!client.busy());
    }
    void httpErrorDoesNotEchoProviderSecrets() {
        Service service; service.status = 401; service.body = "fixture-key snapshot sensitive provider error";
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(service.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(finished.count(), 1);
        const auto answer = result(finished); QVERIFY(answer.error.contains("401")); QVERIFY(!answer.error.contains("fixture-key")); QVERIFY(!answer.error.contains("snapshot")); QVERIFY(answer.text.isEmpty());
    }
    void neverFollowsRedirect() {
        Service source, target; source.status = 307; source.extraHeaders = "Location: " + target.endpoint().toEncoded() + "\r\n";
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(source.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!result(finished).error.isEmpty()); QVERIFY(result(finished).text.isEmpty()); QCOMPARE(source.requests.size(), 1);
        QTest::qWait(30); QCOMPARE(target.requests.size(), 0);
    }
    void responseSizeLimitAbortsWithoutResult() {
        Service service; service.body = QByteArray(1024 * 1024 + 1, 'x');
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(service.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(finished.count(), 1);
        const auto answer = result(finished); QVERIFY(answer.error.contains("1 MiB")); QVERIFY(answer.text.isEmpty()); QVERIFY(!answer.canceled); QVERIFY(!client.busy());
    }
    void brokenTransportIsNotParsedAsSuccess() {
        Service service; service.declaredLength = service.body.size() + 100;
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(service.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!result(finished).error.isEmpty()); QVERIFY(result(finished).text.isEmpty());
    }
    void cancelFinishesOnceAndAllowsExplicitRetry() {
        Service service; service.hold = true;
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished);
        client.start(service.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(service.requests.size(), 1);
        client.cancel(); QTRY_COMPARE(finished.count(), 1); QVERIFY(!client.busy());
        const auto canceled = result(finished); QVERIFY(canceled.canceled); QVERIFY(canceled.text.isEmpty()); QVERIFY(canceled.error.isEmpty());
        client.cancel(); QTest::qWait(30); QCOMPARE(finished.count(), 1); QCOMPARE(service.requests.size(), 1);
        service.hold = false; client.start(service.endpoint(), "model", "fixture-key", "another snapshot"); QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(service.requests.size(), 2); QVERIFY(!result(finished).canceled); QVERIFY(result(finished).error.isEmpty());
    }
    void realTimeoutFinishesOnceAndAllowsExplicitRetry() {
        if (qEnvironmentVariable("STILL_API_TIMEOUT_TEST") != "1") QSKIP("Explicit gate required for the real 60-second timeout test");
        Service service; service.hold = true; QVERIFY(service.server.isListening());
        ApiProofreader client; QSignalSpy finished(&client, &ApiProofreader::finished); QElapsedTimer timer; timer.start();
        client.start(service.endpoint(), "model", "fixture-key", "snapshot");
        QTRY_COMPARE(service.requests.size(), 1); QVERIFY(client.busy()); QCOMPARE(finished.count(), 0);
        QVERIFY(finished.wait(80000)); const qint64 elapsed = timer.elapsed();
        qInfo("API_TIMEOUT_ELAPSED_MS=%lld", static_cast<long long>(elapsed));
        // The default CoarseTimer can fire up to 5% early.
        QVERIFY(elapsed >= 57000); QVERIFY(elapsed <= 80000); QCOMPARE(finished.count(), 1); QVERIFY(!client.busy());
        const auto timedOut = result(finished); QVERIFY(timedOut.error.contains(QStringLiteral("超时")));
        QVERIFY(timedOut.text.isEmpty()); QVERIFY(!timedOut.canceled);
        QCOMPARE(timedOut.inputTokens, qint64(-1)); QCOMPARE(timedOut.outputTokens, qint64(-1)); QCOMPARE(timedOut.totalTokens, qint64(-1));
        QTRY_VERIFY(!service.peers.first() || service.peers.first()->state() == QAbstractSocket::UnconnectedState);
        QTest::qWait(30); QCOMPARE(finished.count(), 1); QCOMPARE(service.requests.size(), 1);
        service.hold = false; client.start(service.endpoint(), "model", "fixture-key", "another snapshot");
        QTRY_COMPARE(finished.count(), 2); QCOMPARE(service.requests.size(), 2); QVERIFY(!client.busy());
        QVERIFY(result(finished).error.isEmpty()); QVERIFY(!result(finished).canceled); QCOMPARE(result(finished).text, QString("corrected"));
    }
    void destructionDisconnectsBeforeAborting() {
        Service service; service.hold = true;
        auto client = std::make_unique<ApiProofreader>(); QSignalSpy finished(client.get(), &ApiProofreader::finished);
        client->start(service.endpoint(), "model", "fixture-key", "snapshot"); QTRY_COMPARE(service.requests.size(), 1);
        client.reset(); QTRY_VERIFY(!service.peers.first() || service.peers.first()->state() == QAbstractSocket::UnconnectedState);
        QCOMPARE(finished.count(), 0); QCOMPARE(service.requests.size(), 1);
    }
};

QTEST_GUILESS_MAIN(ApiProofreaderTest)
#include "api_proofreader_test.moc"
