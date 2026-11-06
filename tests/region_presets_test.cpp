#include "storage/region_presets.h"
#include "storage/store.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

using namespace still;

static RegionPreset preset(const QString &name = "Work") {
    return {name, {"Display-1", "Maker", "Panel", "Serial-1"}, QRectF(10.125, 20.375, 300.625, 200.875)};
}

static QJsonObject presetDocument() {
    return {{"version", 1}, {"presets", QJsonArray{QJsonObject{{"name", "Work"},
        {"screen", QJsonObject{{"name", "Display-1"}, {"manufacturer", "Maker"}, {"model", "Panel"}, {"serialNumber", "Serial-1"}}},
        {"region", QJsonArray{10.125, 20.375, 300.625, 200.875}}}}}};
}

class RegionPresetsTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripPreservesOrderScreenFieldsAndFractionalRegion() {
        QTemporaryDir directory;
        const auto path = directory.filePath("nested/region-presets.json");
        auto first = preset(QStringLiteral("区域一")), second = preset("work");
        second.screen = {};
        second.region = QRectF(0, 0, 1.0 / 3, 7.0 / 9);
        const QVector<RegionPreset> original{first, second, preset("Work"), preset(QString(80, 'x'))};
        QString error;
        QVERIFY2(saveRegionPresets(path, original, &error), qPrintable(error));
        const auto loaded = loadRegionPresets(path, &error);
        QVERIFY2(loaded, qPrintable(error)); QCOMPARE(loaded->size(), original.size());
        for (int i = 0; i < original.size(); ++i) {
            QCOMPARE((*loaded)[i].name, original[i].name);
            QCOMPARE((*loaded)[i].screen.name, original[i].screen.name);
            QCOMPARE((*loaded)[i].screen.manufacturer, original[i].screen.manufacturer);
            QCOMPARE((*loaded)[i].screen.model, original[i].screen.model);
            QCOMPARE((*loaded)[i].screen.serialNumber, original[i].screen.serialNumber);
            QCOMPARE((*loaded)[i].region, original[i].region);
        }
        QVERIFY(saveRegionPresets(path, {second, first}, &error));
        const auto reordered = loadRegionPresets(path, &error);
        QVERIFY(reordered); QCOMPARE(reordered->size(), 2); QCOMPARE((*reordered)[0].name, second.name);
        QVERIFY(saveRegionPresets(path, {}, &error));
        const auto empty = loadRegionPresets(path, &error); QVERIFY(empty); QVERIFY(empty->isEmpty());
    }
    void missingFileIsEmptyWithoutCreatingAnything() {
        QTemporaryDir directory;
        const auto path = directory.filePath("missing/region-presets.json");
        QString error;
        const auto loaded = loadRegionPresets(path, &error);
        QVERIFY(loaded); QVERIFY(loaded->isEmpty()); QVERIFY(error.isEmpty()); QVERIFY(!QFile::exists(path));
        QVERIFY(!QDir(directory.filePath("missing")).exists());
    }
    void damagedFileIsReportedAndNeverOverwritten_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("truncated-json") << QByteArray("{\"version\":1,\"presets\":[");
        QTest::newRow("array-document") << QByteArray("[]");
        QTest::newRow("null-document") << QByteArray("null");
        auto add = [](const QByteArray &name, const QJsonObject &object) {
            QTest::newRow(name.constData()) << QJsonDocument(object).toJson();
        };
        for (const auto &value : {QJsonValue(2), QJsonValue("1"), QJsonValue(true), QJsonValue()}) {
            auto object = presetDocument(); object["version"] = value;
            add("version-" + QByteArray::number(int(value.type())), object);
        }
        auto object = presetDocument(); object.remove("version"); add("missing-version", object);
        object = presetDocument(); object.remove("presets"); add("missing-presets", object);
        object = presetDocument(); object["presets"] = QJsonObject{}; add("presets-object", object);
        object = presetDocument(); object["presets"] = QJsonArray{false}; add("preset-not-object", object);
        auto changeRecord = [&](const QByteArray &name, const QJsonObject &record) {
            auto changed = presetDocument(); auto valid = changed.value("presets").toArray()[0].toObject(); valid["name"] = "Keep";
            changed["presets"] = QJsonArray{valid, record};
            add(name, changed);
        };
        const auto original = presetDocument().value("presets").toArray()[0].toObject();
        for (const auto &value : {QJsonValue(""), QJsonValue("  "), QJsonValue(" Work "), QJsonValue(QString(81, 'x')), QJsonValue(5)}) {
            auto record = original; record["name"] = value;
            changeRecord("invalid-name-" + QByteArray::number(value.toString().size()) + '-' + QByteArray::number(int(value.type())), record);
        }
        auto record = original; record.remove("name"); changeRecord("missing-name", record);
        record = original; record.remove("screen"); changeRecord("missing-screen", record);
        record = original; record["screen"] = false; changeRecord("screen-not-object", record);
        for (const auto &key : {"name", "manufacturer", "model", "serialNumber"}) {
            for (bool missing : {false, true}) {
                record = original; auto screen = record.value("screen").toObject();
                if (missing) screen.remove(key); else screen[key] = 42;
                record["screen"] = screen;
                changeRecord(QByteArray("screen-") + key + (missing ? "-missing" : "-not-string"), record);
            }
        }
        const QVector<QJsonArray> regions{{1, 2, 3}, {1, 2, 3, 4, 5}, {"0", 0, 3, 4},
            {0, false, 3, 4}, {0, 0, QJsonValue(), 4}, {-1, 0, 3, 4}, {0, -1, 3, 4},
            {0, 0, 0, 4}, {0, 0, 3, -4}, {1e308, 0, 1e308, 4}, {0, 1e308, 3, 1e308}};
        for (int i = 0; i < regions.size(); ++i) {
            record = original; record["region"] = regions[i]; changeRecord("region-" + QByteArray::number(i), record);
        }
        record = original; record.remove("region"); changeRecord("missing-region", record);
        record = original; record["region"] = QJsonObject{}; changeRecord("region-not-array", record);
        object = presetDocument(); object["presets"] = QJsonArray{original, original}; add("duplicate-name", object);
    }
    void damagedFileIsReportedAndNeverOverwritten() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QString error;
        QVERIFY(!loadRegionPresets(path, &error)); QVERIFY(!error.isEmpty());
        error.clear(); QVERIFY(!saveRegionPresets(path, {preset("Replacement")}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
    void invalidCandidatesKeepPreviousFile() {
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY(saveRegionPresets(path, {preset()}, &error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        QVector<QVector<RegionPreset>> candidates{{preset("")}, {preset(" ")}, {preset(" Work ")},
            {preset(QString(81, 'x'))}, {preset(), preset()}};
        const qreal nan = std::numeric_limits<qreal>::quiet_NaN(), infinity = std::numeric_limits<qreal>::infinity();
        for (const auto &region : {QRectF(-1, 0, 3, 4), QRectF(0, -1, 3, 4), QRectF(0, 0, 0, 4),
             QRectF(0, 0, 3, -1), QRectF(nan, 0, 3, 4), QRectF(0, infinity, 3, 4),
             QRectF(0, 0, nan, 4), QRectF(0, 0, 3, infinity), QRectF(1e308, 0, 1e308, 4)}) {
            auto invalid = preset(); invalid.region = region; candidates.append({invalid});
        }
        for (const auto &candidate : candidates) {
            error.clear(); QVERIFY(!saveRegionPresets(path, candidate, &error)); QVERIFY(!error.isEmpty());
            QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        }
    }
    void writeFailurePreservesExistingFile() {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY(saveRegionPresets(path, {preset()}, &error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        const auto permissions = QFile::permissions(directory.path());
        const auto restore = qScopeGuard([&] { QFile::setPermissions(directory.path(), permissions); });
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QFile probe(directory.filePath("probe"));
        if (probe.open(QIODevice::WriteOnly)) QSKIP("Directory permissions do not prevent writes in this environment");
        QVERIFY(!saveRegionPresets(path, {preset("Replacement")}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
#else
        QSKIP("Requires Unix directory permissions to force an atomic-write failure");
#endif
    }
    void unreadableExistingFileIsNeverOverwritten() {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        const auto path = directory.filePath("region-presets.json"); QString error;
        QVERIFY(saveRegionPresets(path, {preset()}, &error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        const auto permissions = QFile::permissions(path);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(path, permissions); });
        QVERIFY(QFile::setPermissions(path, QFileDevice::WriteOwner));
        if (file.open(QIODevice::ReadOnly)) QSKIP("File permissions do not prevent reads in this environment");
        QVERIFY(!loadRegionPresets(path, &error)); QVERIFY(!error.isEmpty());
        error.clear(); QVERIFY(!saveRegionPresets(path, {preset("Replacement")}, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(QFile::setPermissions(path, permissions));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
#else
        QSKIP("Requires Unix file permissions to force a read failure");
#endif
    }
    void occupiedParentReportsWriteFailureWithoutChangingIt() {
        QTemporaryDir directory;
        const auto parent = directory.filePath("occupied");
        QFile file(parent); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("keep"), 4); file.close();
        QString error; QVERIFY(!saveRegionPresets(QDir(parent).filePath("region-presets.json"), {preset()}, &error));
        QVERIFY(!error.isEmpty()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep"));
    }
    void serialMatchingSurvivesNameChangesButRequiresUniqueHardwareIdentity() {
        const RegionScreen saved{"Old connector", "Maker", "Panel", "Serial-1"};
        const RegionScreen renamed{"New connector", "Maker", "Panel", "Serial-1"};
        QCOMPARE(matchingPresetScreen(saved, {}), -1);
        QCOMPARE(matchingPresetScreen(saved, {{"Old connector", "Maker", "Panel", "Other serial"}, renamed}), 1);
        QCOMPARE(matchingPresetScreen(saved, {{"New connector", "Other maker", "Panel", "Serial-1"}}), -1);
        QCOMPARE(matchingPresetScreen(saved, {{"New connector", "Maker", "Other panel", "Serial-1"}}), -1);
        QCOMPARE(matchingPresetScreen(saved, {renamed, {"Another connector", "Maker", "Panel", "Serial-1"}}), -1);
        QCOMPARE(matchingPresetScreen({"", "Maker", "Panel", "Serial-1"}, {renamed}), 0);
    }
    void screensWithoutSerialRequireExactUniqueNamesAndUnknownIdentityNeverMatches() {
        const RegionScreen saved{"Display-1", "Maker", "Panel", ""};
        QCOMPARE(matchingPresetScreen(saved, {saved}), 0);
        QCOMPARE(matchingPresetScreen(saved, {{"Display-2", "Maker", "Panel", ""}}), -1);
        QCOMPARE(matchingPresetScreen(saved, {{"Display-1", "Other maker", "Panel", ""}}), -1);
        QCOMPARE(matchingPresetScreen(saved, {{"Display-1", "Maker", "Other panel", ""}}), -1);
        QCOMPARE(matchingPresetScreen(saved, {saved, saved}), -1);
        QCOMPARE(matchingPresetScreen({}, {{}}), -1);
        QCOMPARE(matchingPresetScreen({"", "Maker", "Panel", ""}, {{"", "Maker", "Panel", ""}}), -1);
    }
    void dataDirectoryRecognizesPresetsWithoutOtherMarkers() {
#ifdef Q_OS_WIN
        QTemporaryDir directory;
        const QByteArray local = qgetenv("LOCALAPPDATA"), roaming = qgetenv("APPDATA");
        const QByteArray current = qgetenv("STILL_DATA_DIR"), legacy = qgetenv("PIXCLONE_DATA_DIR");
        const auto restore = qScopeGuard([=] {
            qputenv("LOCALAPPDATA", local); qputenv("APPDATA", roaming);
            if (current.isNull()) qunsetenv("STILL_DATA_DIR"); else qputenv("STILL_DATA_DIR", current);
            if (legacy.isNull()) qunsetenv("PIXCLONE_DATA_DIR"); else qputenv("PIXCLONE_DATA_DIR", legacy);
        });
        qputenv("LOCALAPPDATA", directory.filePath("local").toUtf8());
        qputenv("APPDATA", directory.filePath("roaming").toUtf8());
        qunsetenv("STILL_DATA_DIR"); qunsetenv("PIXCLONE_DATA_DIR");
        QString error;
        const auto oldPath = directory.filePath("roaming/PixClone"), newPath = directory.filePath("local/Still");
        QVERIFY(saveRegionPresets(QDir(oldPath).filePath("region-presets.json"), {preset()}, &error));
        QCOMPARE(dataDirectory(), oldPath);
        QVERIFY(saveRegionPresets(QDir(newPath).filePath("region-presets.json"), {preset()}, &error));
        QCOMPARE(dataDirectory(), newPath);
#else
        QSKIP("Legacy data-directory selection uses isolated Windows environment paths");
#endif
    }
};

QTEST_GUILESS_MAIN(RegionPresetsTest)
#include "region_presets_test.moc"
