#include "storage/pin_session.h"
#include "storage/store.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace still;

static PinState pinState(QColor color = Qt::red) {
    QImage image(120, 80, QImage::Format_ARGB32); image.fill(color);
    return {QUuid::createUuid().toString(QUuid::WithoutBraces), image, QRect(-200, 30, 240, 180), QSize(126, 108), 0.55, false, {}};
}
static PinState textPinState(const QString &text, const QString &html = {}) {
    auto state = pinState(); state.image = {}; state.text = text; state.html = html;
    return state;
}

class PinSessionTest : public QObject {
    Q_OBJECT
private slots:
    void closedPinsKeepIndependentStateAndRestoreInReverseOrder() {
        PinSession session("unused");
        auto first = pinState(), second = pinState(Qt::green);
        session.rememberClosed(first); session.rememberClosed(second);
        second.image.fill(Qt::blue);
        QVERIFY(session.canRestoreClosed());
        const auto latest = session.takeClosed(); QVERIFY(latest);
        QCOMPARE(latest->image.pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(latest->geometry, second.geometry); QCOMPARE(latest->opacity, second.opacity);
        QCOMPARE(session.takeClosed()->id, first.id);
        QVERIFY(!session.canRestoreClosed()); QVERIFY(!session.takeClosed());
    }
    void closeStackHasCountAndContentBudgets() {
        PinSession session("unused");
        for (int i = 0; i < 25; ++i) session.rememberClosed(pinState());
        int count = 0; while (session.takeClosed()) ++count;
        QCOMPARE(count, 20);
        auto state = pinState(); state.image = QImage(2048, 2048, QImage::Format_ARGB32); state.image.fill(Qt::white);
        for (int i = 0; i < 20; ++i) session.rememberClosed(state);
        count = 0; while (session.takeClosed()) ++count;
        QCOMPARE(count, 16);
        state = textPinState(QString(8 * 1024 * 1024, 'x'));
        for (int i = 0; i < 20; ++i) session.rememberClosed(state);
        state.text[0] = 'y';
        const auto latest = session.takeClosed(); QVERIFY(latest); QCOMPARE(latest->text[0], QChar('x'));
        count = 1; while (session.takeClosed()) ++count;
        QCOMPARE(count, 16);
        state = textPinState(QString(4 * 1024 * 1024, 'x'), QString(4 * 1024 * 1024, 'h'));
        for (int i = 0; i < 20; ++i) session.rememberClosed(state);
        state.html[0] = 'y';
        const auto rich = session.takeClosed(); QVERIFY(rich); QCOMPARE(rich->html[0], QChar('h'));
        count = 1; while (session.takeClosed()) ++count;
        QCOMPARE(count, 16);
    }
    void roundTripIncludesPixelsAndWindowState() {
        QTemporaryDir directory;
        PinSession session(directory.path());
        const auto first = pinState(), second = pinState(Qt::transparent);
        QString error; QVERIFY(session.save({first, second}, &error));
        const auto result = PinSession(directory.path()).load();
        QVERIFY(result.errors.isEmpty()); QCOMPARE(result.pins.size(), 2);
        QCOMPARE(result.pins[0].id, first.id); QCOMPARE(result.pins[0].image, first.image);
        QCOMPARE(result.pins[0].geometry, first.geometry); QCOMPARE(result.pins[0].originalSize, first.originalSize);
        QCOMPARE(result.pins[0].opacity, first.opacity); QCOMPARE(result.pins[0].alwaysOnTop, first.alwaysOnTop);
        QCOMPARE(result.pins[1].image, second.image);
    }
    void mixedSessionPreservesTextBytesAndWindowState() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        QString text = QChar(0xfeff) + QString::fromUtf8("  <b>文字 😀</b>\t\r\nsecond\n");
        text.append(QChar(0)); text += " \t ";
        QString html = QChar(0xfeff) + QString::fromUtf8(" \t<b>显示文字 😀</b>\r\n");
        html.append(QChar(0)); html += " \t ";
        const auto image = pinState(), plain = textPinState(text), rich = textPinState(" original fallback\r\n\t ", html);
        QVERIFY(session.save({image, plain, rich}, &error));
        const auto index = readJson(directory.filePath("session.json")); QCOMPARE(index.value("version").toInt(), 3);
        const auto records = index.value("pins").toArray();
        QCOMPARE(records[0].toObject().value("kind").toString(), QString("image"));
        const auto record = records[1].toObject(); QCOMPARE(record.value("kind").toString(), QString("text"));
        QFile file(directory.filePath(record.value("text").toString())); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), text.toUtf8());
        const auto richRecord = records[2].toObject(); QCOMPARE(richRecord.value("kind").toString(), QString("html"));
        QFile htmlFile(directory.filePath(richRecord.value("html").toString())); QVERIFY(htmlFile.open(QIODevice::ReadOnly));
        QCOMPARE(htmlFile.readAll(), html.toUtf8());
        QFile fallback(directory.filePath(richRecord.value("text").toString())); QVERIFY(fallback.open(QIODevice::ReadOnly));
        QCOMPARE(fallback.readAll(), rich.text.toUtf8());
        const auto loaded = session.load(); QVERIFY(loaded.errors.isEmpty()); QCOMPARE(loaded.pins.size(), 3);
        QCOMPARE(loaded.pins[0].image, image.image); QVERIFY(loaded.pins[0].text.isEmpty());
        const auto restored = loaded.pins[1]; QVERIFY(restored.image.isNull()); QCOMPARE(restored.text, text);
        QVERIFY(restored.html.isEmpty());
        QCOMPARE(restored.id, plain.id); QCOMPARE(restored.geometry, plain.geometry);
        QCOMPARE(restored.originalSize, plain.originalSize); QCOMPARE(restored.opacity, plain.opacity);
        QCOMPARE(restored.alwaysOnTop, plain.alwaysOnTop);
        QVERIFY(loaded.pins[2].image.isNull()); QCOMPARE(loaded.pins[2].id, rich.id);
        QCOMPARE(loaded.pins[2].text, rich.text); QCOMPARE(loaded.pins[2].html, rich.html);
    }
    void legacyImageSessionLoadsAndIsReplacedByVersionThree() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto previous = pinState(); QVERIFY(session.save({previous}, &error));
        auto index = readJson(directory.filePath("session.json")); auto records = index.value("pins").toArray();
        auto record = records[0].toObject(); const auto image = record.value("image").toString();
        record.remove("kind"); records[0] = record; index["version"] = 1; index["pins"] = records;
        QVERIFY(writeJson(directory.filePath("session.json"), index));
        const auto loaded = session.load(); QVERIFY(loaded.errors.isEmpty()); QCOMPARE(loaded.pins.size(), 1);
        QCOMPARE(loaded.pins[0].id, previous.id); QCOMPARE(loaded.pins[0].image, previous.image);
        const auto next = textPinState("replacement"); QVERIFY(session.save({next}, &error));
        QCOMPARE(readJson(directory.filePath("session.json")).value("version").toInt(), 3);
        QCOMPARE(session.load().pins[0].text, next.text); QVERIFY(!QFileInfo::exists(directory.filePath(image)));
    }
    void versionTwoImageAndPlainTextSessionLoadsAndIsReplaced() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto image = pinState(), plain = textPinState(" previous\r\n\t ");
        QVERIFY(session.save({image, plain}, &error));
        auto index = readJson(directory.filePath("session.json")); index["version"] = 2;
        const auto previous = index.value("pins").toArray();
        QVERIFY(writeJson(directory.filePath("session.json"), index));
        const auto loaded = session.load(); QVERIFY(loaded.errors.isEmpty()); QCOMPARE(loaded.pins.size(), 2);
        QCOMPARE(loaded.pins[0].image, image.image); QCOMPARE(loaded.pins[1].text, plain.text); QVERIFY(loaded.pins[1].html.isEmpty());
        const auto rich = textPinState("fallback", "<b>next</b>"); QVERIFY(session.save({rich}, &error));
        QCOMPARE(readJson(directory.filePath("session.json")).value("version").toInt(), 3);
        QCOMPARE(session.load().pins[0].html, rich.html);
        QVERIFY(!QFileInfo::exists(directory.filePath(previous[0].toObject().value("image").toString())));
        QVERIFY(!QFileInfo::exists(directory.filePath(previous[1].toObject().value("text").toString())));
    }
    void replacementRemovesOnlyThePreviouslyCommittedContent() {
        QTemporaryDir directory;
        PinSession session(directory.path()); QString error;
        QVERIFY(session.save({pinState()}, &error));
        const auto previous = readJson(directory.filePath("session.json")).value("pins").toArray()[0].toObject().value("image").toString();
        QVERIFY(QDir().mkpath(directory.filePath("unrelated")));
        QFile other(directory.filePath("unrelated/keep.txt")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write("keep"); other.close();
        const auto next = textPinState("replacement", "<b>replacement</b>"); QVERIFY(session.save({next}, &error));
        QVERIFY(!QFileInfo::exists(directory.filePath(previous))); QVERIFY(QFileInfo::exists(other.fileName()));
        QCOMPARE(session.load().pins[0].text, next.text); QCOMPARE(session.load().pins[0].html, next.html);
        QVERIFY(session.save({}, &error)); QVERIFY(session.load().pins.isEmpty());
        QVERIFY(QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(QFileInfo::exists(other.fileName()));
    }
    void failedEncodingPreservesTheLastCommittedSession() {
        QTemporaryDir directory;
        PinSession session(directory.path()); QString error;
        const auto previous = pinState(); QVERIFY(session.save({previous}, &error));
        QFile manifest(directory.filePath("session.json")); QVERIFY(manifest.open(QIODevice::ReadOnly)); const auto before = manifest.readAll(); manifest.close();
        const auto folders = QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        auto invalid = pinState(); invalid.image = {};
        QVERIFY(!session.save({pinState(Qt::blue), invalid}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(manifest.open(QIODevice::ReadOnly)); QCOMPARE(manifest.readAll(), before);
        QCOMPARE(session.load().pins[0].image, previous.image);
        QCOMPARE(QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot), folders);
    }
    void invalidTextPreservesTheLastCommittedSession() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto previous = textPinState("keep\r\n\t "); QVERIFY(session.save({previous}, &error));
        QFile manifest(directory.filePath("session.json")); QVERIFY(manifest.open(QIODevice::ReadOnly)); const auto before = manifest.readAll(); manifest.close();
        const auto folders = QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        const QString invalid[] = {QString(), QString(QChar(0xd800)), QString(maximumTextPinBytes / 3 + 1, QChar(0x4e2d))};
        for (const auto &text : invalid) {
            QVERIFY(!session.save({pinState(Qt::blue), textPinState(text)}, &error)); QVERIFY(!error.isEmpty());
            QVERIFY(manifest.open(QIODevice::ReadOnly)); QCOMPARE(manifest.readAll(), before); manifest.close();
            QCOMPARE(session.load().pins[0].text, previous.text);
            QCOMPARE(QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot), folders);
        }
    }
    void textByteLimitAllowsTheExactBoundary() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto state = textPinState(QString(maximumTextPinBytes, 'x'));
        QVERIFY(session.save({state}, &error));
        const auto loaded = session.load(); QVERIFY(loaded.errors.isEmpty()); QCOMPARE(loaded.pins.size(), 1);
        QCOMPARE(loaded.pins[0].text, state.text);
    }
    void invalidHtmlPreservesTheLastCommittedSession() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto previous = textPinState(" keep\r\n\t ", "<b>keep</b>"); QVERIFY(session.save({previous}, &error));
        QFile manifest(directory.filePath("session.json")); QVERIFY(manifest.open(QIODevice::ReadOnly)); const auto before = manifest.readAll(); manifest.close();
        const auto folders = QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        const QString half(maximumTextPinBytes / 2, 'x');
        const PinState invalid[] = {textPinState({}, "<b>empty fallback</b>"), textPinState("fallback", QString(QChar(0xd800))), textPinState(half, half + 'x')};
        for (const auto &state : invalid) {
            QVERIFY(!session.save({pinState(Qt::blue), state}, &error)); QVERIFY(!error.isEmpty());
            QVERIFY(manifest.open(QIODevice::ReadOnly)); QCOMPARE(manifest.readAll(), before); manifest.close();
            const auto loaded = session.load(); QVERIFY(loaded.errors.isEmpty()); QCOMPARE(loaded.pins.size(), 1);
            QCOMPARE(loaded.pins[0].text, previous.text); QCOMPARE(loaded.pins[0].html, previous.html);
            QCOMPARE(QDir(directory.path()).entryList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot), folders);
        }
    }
    void htmlAndFallbackShareTheByteLimit() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const QString fallback = "fallback";
        const auto available = maximumTextPinBytes - fallback.toUtf8().size();
        const auto rich = textPinState(fallback, QString(available / 3, QChar(0x4e2d)) + QString(available % 3, 'x'));
        const auto image = pinState(Qt::green); QVERIFY(session.save({rich, image}, &error));
        const auto loaded = session.load(); QVERIFY(loaded.errors.isEmpty()); QCOMPARE(loaded.pins.size(), 2);
        QCOMPARE(loaded.pins[0].text, rich.text); QCOMPARE(loaded.pins[0].html, rich.html);
        const auto index = readJson(directory.filePath("session.json"));
        const auto html = index.value("pins").toArray()[0].toObject().value("html").toString();
        QFile file(directory.filePath(html)); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append)); QCOMPARE(file.write("x"), 1); file.close();
        const auto oversized = session.load(); QCOMPARE(oversized.pins.size(), 1); QCOMPARE(oversized.pins[0].id, image.id);
        QCOMPARE(oversized.errors.size(), 1); QVERIFY(oversized.errors[0].contains(html));
        QCOMPARE(readJson(directory.filePath("session.json")), index);
    }
    void malformedIndexIsReportedAndNeverOverwritten() {
        QTemporaryDir directory;
        PinSession session(directory.path()); QString error;
        QVERIFY(session.save({pinState()}, &error));
        const auto original = readJson(directory.filePath("session.json"));
        for (int mode = 0; mode < 11; ++mode) {
            auto changed = original; auto records = changed.value("pins").toArray(); auto record = records[0].toObject();
            if (mode == 0) changed["version"] = 42;
            if (mode == 1) record["image"] = "../outside.png";
            if (mode == 2) record["opacity"] = 2;
            if (mode == 4) record["kind"] = "unknown";
            if (mode == 5) { record["kind"] = "text"; record["text"] = "../outside.txt"; }
            if (mode == 6) { record["kind"] = "text"; record["text"] = record.value("image"); }
            if (mode >= 7) {
                record["kind"] = "html"; const auto text = record.value("image").toString().chopped(4) + ".txt"; record["text"] = text;
                if (mode == 7) record["html"] = "../outside.html";
                if (mode == 8) record["html"] = text.chopped(4) + "1.html";
                if (mode == 9) { record["html"] = text.chopped(4) + ".html"; changed["version"] = 2; }
            }
            records[0] = record; if (mode == 3) records.append(record); changed["pins"] = records;
            QVERIFY(writeJson(directory.filePath("session.json"), changed));
            QVERIFY(!session.load().errors.isEmpty());
            QVERIFY(!session.save({pinState(Qt::blue)}, &error));
            QCOMPARE(readJson(directory.filePath("session.json")), changed);
        }
    }
    void missingImageDoesNotPreventOtherPinsFromLoading() {
        QTemporaryDir directory;
        PinSession session(directory.path()); QString error;
        const auto first = pinState(), second = pinState(Qt::green);
        QVERIFY(session.save({first, second}, &error));
        const auto index = readJson(directory.filePath("session.json"));
        const QString image = index.value("pins").toArray()[0].toObject().value("image").toString();
        QVERIFY(QFile::remove(directory.filePath(image)));
        const auto loaded = session.load();
        QCOMPARE(loaded.pins.size(), 1); QCOMPARE(loaded.pins[0].id, second.id);
        QCOMPARE(loaded.errors.size(), 1); QVERIFY(loaded.errors[0].contains(image));
        QCOMPARE(readJson(directory.filePath("session.json")), index);
    }
    void missingOrInvalidTextDoesNotPreventOtherPinsFromLoading() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto plain = textPinState("text"), image = pinState(Qt::green);
        QVERIFY(session.save({plain, image}, &error));
        const auto index = readJson(directory.filePath("session.json"));
        const auto text = index.value("pins").toArray()[0].toObject().value("text").toString();
        QFile file(directory.filePath(text)); QVERIFY(file.remove());
        const QByteArray invalid[] = {QByteArray(), QByteArray::fromHex("c0af"), QByteArray::fromHex("f09f98")};
        for (int mode = 0; mode < 5; ++mode) {
            if (mode > 0) {
                QVERIFY(file.open(QIODevice::WriteOnly));
                if (mode == 4) QVERIFY(file.resize(maximumTextPinBytes + 1));
                else QCOMPARE(file.write(invalid[mode - 1]), invalid[mode - 1].size());
                file.close();
            }
            const auto loaded = session.load(); QCOMPARE(loaded.pins.size(), 1); QCOMPARE(loaded.pins[0].id, image.id);
            QCOMPARE(loaded.errors.size(), 1); QVERIFY(loaded.errors[0].contains(text));
            QCOMPARE(readJson(directory.filePath("session.json")), index);
        }
    }
    void missingOrInvalidHtmlDoesNotRestoreAPartialPin() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto rich = textPinState("fallback", "<b>text</b>"), image = pinState(Qt::green);
        QVERIFY(session.save({rich, image}, &error));
        const auto index = readJson(directory.filePath("session.json"));
        const auto record = index.value("pins").toArray()[0].toObject();
        const auto html = record.value("html").toString(); QFile file(directory.filePath(html)); QVERIFY(file.remove());
        const QByteArray invalid[] = {QByteArray(), QByteArray::fromHex("c0af"), QByteArray::fromHex("f09f98")};
        for (int mode = 0; mode < 4; ++mode) {
            if (mode > 0) { QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(invalid[mode - 1]), invalid[mode - 1].size()); file.close(); }
            const auto loaded = session.load(); QCOMPARE(loaded.pins.size(), 1); QCOMPARE(loaded.pins[0].id, image.id);
            QCOMPARE(loaded.errors.size(), 1); QVERIFY(loaded.errors[0].contains(html));
            QCOMPARE(readJson(directory.filePath("session.json")), index);
        }
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("<b>text</b>"), 11); file.close();
        QFile fallback(directory.filePath(record.value("text").toString())); QVERIFY(fallback.open(QIODevice::WriteOnly)); fallback.close();
        const auto emptyFallback = session.load(); QCOMPARE(emptyFallback.pins.size(), 1); QCOMPARE(emptyFallback.pins[0].id, image.id);
        QCOMPARE(emptyFallback.errors.size(), 1); QVERIFY(emptyFallback.errors[0].contains(fallback.fileName()));
    }
    void restoreSkipsOpenPinsAndStopsAtTheAvailableCapacity() {
        QTemporaryDir directory; PinSession session(directory.path()); QString error;
        const auto first = pinState(), second = pinState(Qt::green), third = pinState(Qt::blue);
        QVERIFY(session.save({first, second, third}, &error));
        const auto limited = session.load({first.id}, 1);
        QCOMPARE(limited.pins.size(), 1); QCOMPARE(limited.pins[0].id, second.id); QVERIFY(limited.limited);
        const auto exhausted = session.load({first.id, second.id, third.id}, 0);
        QVERIFY(exhausted.pins.isEmpty()); QVERIFY(!exhausted.limited); QVERIFY(exhausted.errors.isEmpty());
        QCOMPARE(session.load().pins.size(), 3);
    }
};
QTEST_MAIN(PinSessionTest)
#include "pin_session_test.moc"
