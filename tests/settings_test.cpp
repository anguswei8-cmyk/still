#include "app/application.h"
#include "clipboard/clipboard_archive.h"
#include "clipboard/clipboard_controller.h"
#include "imaging/editor.h"
#include "imaging/ocr.h"
#include "imaging/ocr_result_dialog.h"
#include "storage/history.h"
#include "ui/main_window.h"
#include "capture/overlay.h"
#include "capture/capture_presets.h"
#include "imaging/pin.h"
#include "imaging/text_pin.h"
#include "settings/settings_apply.h"
#include "settings/settings_backup.h"
#include "capture/scroll_session.h"
#include "storage/store.h"
#include "storage/pin_session.h"
#include "tracked_image.h"
#include "ui/i18n.h"
#include <QAction>
#include <QStyleHints>

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QFontInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QToolBar>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QStyleOptionComboBox>
#include <QTextLayout>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTabWidget>
#include <QScrollBar>
#include <QProcess>
#include <QProcessEnvironment>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QContextMenuEvent>
#include <cmath>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

using namespace still;
namespace still { void setPasteTargetForTest(quintptr target); quintptr lastPasteTargetForTest(); void resetCapturedScreensForTest(); int capturedScreenCountForTest(); void setCapturedImageForTest(const QImage &image); void setOcrResultForTest(const OcrResult &result); int ocrCallCountForTest(); }
namespace still {
void setStartupStatusForTest(StartupState state, const QString &message = {}, const QString &settingsLabel = {});
void setStartupFailureForTest(const QString &message, bool changesState = false);
int startupChangeCountForTest();
int startupSettingsOpenedForTest();
}
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *match = findItem(child, name)) return match;
    return nullptr;
}

static RegionScreen presetScreenIdentity(const QScreen *screen) {
    return {screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber()};
}

static QScreen *identifiablePresetScreen() {
    const auto screens = QGuiApplication::screens(); QVector<RegionScreen> identities;
    for (const auto *screen : screens) identities.append(presetScreenIdentity(screen));
    QScreen *result = nullptr;
    for (int i = 0; i < screens.size(); ++i) if (matchingPresetScreen(identities[i], identities) == i) {
        if (!result) result = screens[i];
        if (screens[i]->geometry().left() < 0 || screens[i]->geometry().top() < 0) return screens[i];
    }
    return result;
}

class SettingsTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QByteArray previousDirectory_;
    Application *controller_ = nullptr;
    MainWindow *window_ = nullptr;
    SettingsApply *apply_ = nullptr;
    QFont initialBodyFont_;
    QQuickItem *item(const QString &name) const {
        return findItem(window_->view()->quickWindow()->contentItem(), name);
    }
    void click(QQuickItem *control, Qt::MouseButton button = Qt::LeftButton) {
        QVERIFY(control); QVERIFY(control->isVisible());
        QTest::mouseClick(window_->view(), button, {}, control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
    }
    void movePointerTo(QQuickItem *control) {
        // Send to the QML window so hover does not depend on the desktop cursor.
        QTest::mouseMove(window_->view()->quickWindow(), control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
    }
    void locateWindow() {
        window_ = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = qobject_cast<MainWindow *>(widget)) window_ = window;
    }
    void openPage(int page) {
        const bool preferences = page != 0 && page != 3;
        const bool currentPreferences = window_->page() != 0 && window_->page() != 3;
        if (preferences && !currentPreferences) click(item("PreferencesEntry"));
        if (!preferences && currentPreferences) click(item("BackToLibrary"));
        QTRY_VERIFY(item("Navigation" + QString::number(page)));
        click(item("Navigation" + QString::number(page)));
    }
    QString reviewPath(const QString &name) const {
        const QString path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.impeccable/review")
            + (QGuiApplication::platformName() == "offscreen" ? "/offscreen" : qEnvironmentVariable("QT_SCALE_FACTOR") == "1.5" ? "/hidpi" : "");
        QDir().mkpath(path); return path + "/qml-" + name + ".png";
    }
    QByteArray fixtureBytes(const QString &path) const {
        QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    void polishSettingsSurface() {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!window_->view()->grabFramebuffer().isNull());
    }
    void scrollSettingsBackupIntoView() {
        window_->setPage(1); QTest::qWait(30);
        auto *page = item("SettingsPage"); QVERIFY(page);
        auto *flickable = page->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        flickable->setProperty("contentY", qMax(0.0, flickable->property("contentHeight").toReal() - flickable->height()));
        auto *entry = item("SettingsBackup"); QVERIFY(entry); QTest::qWait(30);
        const QRectF viewport(page->mapToScene(QPointF()), page->size());
        QVERIFY(viewport.contains(entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2))));
    }
    void scrollSettingIntoView(QQuickItem *control) {
        QVERIFY(control);
        polishSettingsSurface();
        auto *page = control;
        while (page && page->objectName() != "SettingsPage") page = page->parentItem();
        QVERIFY(page);
        auto *flickable = page->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        const qreal target = flickable->property("contentY").toReal() + control->mapToScene(QPointF()).y()
            - page->mapToScene(QPointF()).y() - page->height() / 2;
        flickable->setProperty("contentY", qBound(0.0, target, qMax(0.0, flickable->property("contentHeight").toReal() - flickable->height())));
        polishSettingsSurface();
        const QRectF viewport(page->mapToScene(QPointF()), page->size());
        QVERIFY(viewport.contains(control->mapToScene(QPointF(control->width() / 2, control->height() / 2))));
    }
    void revealSingleDefault(const QString &group, const QString &key) {
        auto *restore = item("reset_" + group + '_' + key); QVERIFY(restore);
        scrollSettingIntoView(restore);
        QTest::mouseMove(window_->view()->quickWindow(), QPoint(1, 1));
        QCoreApplication::processEvents(); movePointerTo(restore);
        QTRY_VERIFY(restore->property("enabled").toBool());
        QTRY_VERIFY(restore->opacity() > 0.9);
    }
    bool confirmSingleDefault(QQuickItem *restore, QMessageBox::StandardButton response, const QString &screenshot = {}, const std::function<void()> &beforeAnswer = {}) {
        bool answered = false; QTimer answer; QElapsedTimer deadline; deadline.start();
        connect(&answer, &QTimer::timeout, this, [&] {
            auto *confirmation = window_->findChild<QMessageBox *>("RestoreSettingConfirmation");
            if (confirmation && confirmation->isVisible()) {
                answer.stop();
                answered = confirmation->defaultButton() == confirmation->button(QMessageBox::No)
                    && confirmation->textFormat() == Qt::PlainText && (screenshot.isEmpty() || confirmation->grab().save(screenshot));
                if (beforeAnswer) beforeAnswer();
                QTest::mouseClick(confirmation->button(response), Qt::LeftButton);
            } else if (deadline.elapsed() > 3000) {
                answer.stop(); if (auto *modal = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) modal->reject();
            }
        });
        answer.start(10); click(restore);
        return answered;
    }
    bool chooseSettingsBackupFile(QDialog *panel, const char *trigger, const char *name, const QString &path) {
        bool selected = false; int phase = 0; QTimer answer; QElapsedTimer deadline; deadline.start();
        connect(&answer, &QTimer::timeout, panel, [&] {
            auto *chooser = panel->findChild<QFileDialog *>(name);
            if (deadline.elapsed() > 3000) {
                qWarning() << "Backup file chooser did not reach its input/selection state:" << name << "phase:" << phase
                           << "selected:" << (chooser ? chooser->selectedFiles() : QStringList()) << "expected:" << path;
                answer.stop(); selected = false; if (auto *modal = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) modal->reject(); return;
            }
            if (!chooser || !chooser->isVisible()) return;
            auto *filename = chooser->findChild<QLineEdit *>("fileNameEdit");
            auto *buttons = chooser->findChild<QDialogButtonBox *>();
            auto *accept = buttons ? buttons->button(chooser->acceptMode() == QFileDialog::AcceptSave ? QDialogButtonBox::Save : QDialogButtonBox::Open) : nullptr;
            if (!filename || !filename->isVisible() || !accept) return;
            if (phase == 0) {
                connect(chooser, &QDialog::accepted, &answer, [&, chooser] { selected = chooser->selectedFiles() == QStringList{path}; answer.stop(); });
                chooser->setDirectory(QFileInfo(path).absolutePath()); phase = 1;
            } else if (phase == 1) {
                if (chooser->directory().absolutePath() != QFileInfo(path).absolutePath()) return;
                QTest::mouseClick(filename, Qt::LeftButton); if (!filename->hasFocus()) return;
                QTest::keyClick(filename, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(filename, QFileInfo(path).fileName()); phase = 2;
            } else if (phase == 2 && chooser->selectedFiles() == QStringList{path} && accept->isEnabled()) {
                phase = 3; QTest::mouseClick(accept, Qt::LeftButton);
            }
        });
        answer.start(10); panel->findChild<QPushButton *>(trigger)->click();
        return selected;
    }
    bool confirmSettingsBackup(QDialog *panel, const char *trigger, QMessageBox::StandardButton response, const QString &screenshot = {}) {
        bool answered = false; QTimer answer; QElapsedTimer deadline; deadline.start();
        connect(&answer, &QTimer::timeout, panel, [&] {
            auto *confirmation = panel->findChild<QMessageBox *>("SettingsBackupConfirmation");
            if (confirmation && confirmation->isVisible()) {
                answer.stop();
                answered = confirmation->defaultButton() == confirmation->button(QMessageBox::No)
                    && confirmation->textFormat() == Qt::PlainText && (screenshot.isEmpty() || confirmation->grab().save(screenshot));
                QTest::mouseClick(confirmation->button(response), Qt::LeftButton);
            } else if (deadline.elapsed() > 3000) {
                answer.stop(); if (auto *modal = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) modal->reject();
            }
        });
        answer.start(10); panel->findChild<QPushButton *>(trigger)->click();
        return answered;
    }
    bool spinArrowsVisible(QSpinBox *input) const {
        QStyleOptionSpinBox option; option.initFrom(input); option.frame = input->hasFrame(); option.buttonSymbols = input->buttonSymbols(); option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
        const auto image = input->grab().toImage(); const auto foreground = input->palette().color(QPalette::ButtonText);
        for (const auto control : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown}) {
            const auto sample = input->style()->subControlRect(QStyle::CC_SpinBox, &option, control, input).adjusted(4, 2, -4, -2);
            const auto pixels = pixelRect(QRectF(sample), input->size(), image.size()); bool found = false;
            for (int y = pixels.top(); y <= pixels.bottom() && !found; ++y) for (int x = pixels.left(); x <= pixels.right(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.alpha() == 255 && qAbs(color.red() - foreground.red()) <= 32 && qAbs(color.green() - foreground.green()) <= 32 && qAbs(color.blue() - foreground.blue()) <= 32) { found = true; break; }
            }
            if (!found) return false;
        }
        return true;
    }
    bool comboArrowVisible(QComboBox *input) const {
        QStyleOptionComboBox option; option.initFrom(input); option.editable = input->isEditable(); option.frame = input->hasFrame();
        const auto sample = input->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, input).adjusted(4, 2, -4, -2);
        const auto image = input->grab().toImage(); const auto pixels = pixelRect(QRectF(sample), input->size(), image.size());
        const auto luminance = [](const QColor &color) {
            const auto linear = [](qreal channel) { return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4); };
            return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
        };
        const qreal background = luminance(image.pixelColor(pixels.topLeft()));
        for (int y = pixels.top(); y <= pixels.bottom(); ++y) for (int x = pixels.left(); x <= pixels.right(); ++x) {
            const auto color = image.pixelColor(x, y); const qreal foreground = luminance(color);
            if (color.alpha() == 255 && (qMax(background, foreground) + 0.05) / (qMin(background, foreground) + 0.05) >= 3) return true;
        }
        return false;
    }
    bool ratioPopupClosesWithEscape(QComboBox *input, const QString &path = {}) const {
        auto *view = input->view(); QPointer<QWidget> popup = view->window();
        if (!popup->isVisible()) input->showPopup();
        if (!QTest::qWaitFor([&] { return popup && popup->isVisible(); }, 1000)) return false;
        qInfo() << "Ratio popup backend=Qt";
        int textWidth = 0; for (int i = 0; i < input->count(); ++i) textWidth = qMax(textWidth, view->fontMetrics().horizontalAdvance(input->itemText(i)));
        const int available = view->viewport()->width(), required = qMax(view->sizeHintForColumn(0), textWidth + input->iconSize().width() + 4); const bool fits = available >= required;
        if (!fits) qWarning() << "Ratio popup width evidence: viewport=" << available << "required=" << required;
        const bool saved = path.isEmpty() || popup->grab().save(path); QTest::keyClick(view, Qt::Key_Escape);
        return QTest::qWaitFor([&] { return !popup || !popup->isVisible(); }, 1000) && saved && fits;
    }
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        // The offscreen plugin does not discover Windows fonts, so use the same UI metrics as the app.
        if (QGuiApplication::platformName() == "offscreen") {
            for (const auto &name : {"msyh.ttc", "msyhbd.ttc"})
                QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QString("Fonts/") + name));
        }
#endif
        QVERIFY(directory_.isValid()); previousDirectory_ = qgetenv("STILL_DATA_DIR");
        qputenv("STILL_DATA_DIR", directory_.path().toUtf8());
        QVERIFY(writeJson(settingsPath(), {{"capture", QJsonObject{{"afterSelectAction", "unknown"}}}, {"ocr", QJsonObject{{"language", false}}}, {"future", QJsonObject{{"keep", 42}}}, {"hotkeys", QJsonObject{{"clipboardHistory", "Ctrl+Shift+Space"}}}, {"appearance", QJsonObject{{"language", "zh_CN"}, {"theme", "light"}}}}));
        const int startupChanges = startupChangeCountForTest();
        controller_ = new Application(nullptr, false); QTest::qWait(100); locateWindow();
        QCOMPARE(startupChangeCountForTest(), startupChanges);
        QVERIFY(window_); QCOMPARE(window_->view()->status(), QQuickWidget::Ready);
        QVERIFY(window_->view()->rootObject()); QCOMPARE(window_->page(), 0);
        QVERIFY(item("ClipboardSearch")); QVERIFY(!window_->findChild<QWidget *>("MainSidebar"));
        QCOMPARE(window_->form().value("capture").toMap().value("afterSelectAction").toString(), QString("annotate"));
        QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("zh-Hans-CN"));
        apply_ = controller_->findChild<SettingsApply *>(); QVERIFY(apply_);
        if (QGuiApplication::platformName() == "windows")
            QCOMPARE(QFontInfo(item("PageTitle")->property("font").value<QFont>()).family(), QString("Microsoft YaHei UI"));
    }
    void savePreferencesHavePreviewAndFormatControls() {
        openPage(1); window_->resize(1000, 740);
        auto *page = item("SettingsPage"); QVERIFY(page);
        auto *flickable = page->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        QTest::qWait(30);
        flickable->setProperty("contentY", flickable->property("contentHeight").toReal() - flickable->height());
        auto *pattern = item("save_nameTemplate"); QVERIFY(pattern);
        pattern->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(window_->view(), "work_{date}_{time}");
        QTest::keyClick(window_->view(), Qt::Key_Tab);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("save").toObject().value("nameTemplate").toString(), QString("work_{date}_{time}"));
        QVERIFY(item("saveNamePreview")->property("text").toString().startsWith("work_"));
        QVERIFY(item("saveNamePreview")->property("text").toString().endsWith(".png"));
        QVERIFY(item("save_pngCompression")->isVisible()); QVERIFY(!item("save_jpegQuality")->isVisible());
        auto *format = item("save_format"); format->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Down); QTRY_VERIFY(!apply_->busy());
        QTRY_VERIFY(item("save_jpegQuality")->isVisible()); QVERIFY(!item("save_pngCompression")->isVisible());
        QVERIFY(item("saveNamePreview")->property("text").toString().endsWith(".jpg"));
        auto *quality = item("save_jpegQuality"); quality->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Down); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("save").toObject().value("jpegQuality").toInt(), 94);
        auto *asFile = item("save_copyAsFile"); QVERIFY(asFile);
        QVERIFY(!asFile->property("checked").toBool()); click(asFile); QTRY_VERIFY(!apply_->busy());
        QVERIFY(readJson(settingsPath()).value("save").toObject().value("copyAsFile").toBool());
        click(asFile); QTRY_VERIFY(!apply_->busy());
        // Leave the shared fixture's defaults intact for the remaining settings tests.
        window_->setSetting("save", "nameTemplate", "still_{date}_{time}_{ms}");
        window_->setSetting("save", "format", "png"); window_->setSetting("save", "jpegQuality", 95);
        QTRY_VERIFY(!apply_->busy());
    }
    void savePreferencesUpdateOpenEditorsAndPins() {
        QImage image(120, 80, QImage::Format_RGB32); image.fill(Qt::green);
        const auto source = directory_.filePath("save-source.png"); QVERIFY(image.save(source));
        controller_->openImage(source);
        Editor *editor = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Editor *>(widget)) editor = candidate;
        QVERIFY(editor); editor->pinRequested(image);
        Pin *pin = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Pin *>(widget)) pin = candidate;
        QVERIFY(pin);
        window_->setSetting("save", "dir", directory_.filePath("exports"));
        window_->setSetting("save", "nameTemplate", "live"); window_->setSetting("save", "format", "jpeg");
        QTRY_VERIFY(!apply_->busy());
        auto *quick = editor->findChild<QAction *>("quickSave"); QVERIFY(quick); quick->trigger();
        QVERIFY(!QImage(directory_.filePath("exports/live.jpg")).isNull());
        QTest::keyClick(pin, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!QImage(directory_.filePath("exports/live-2.jpg")).isNull());
        window_->setSetting("save", "copyAsFile", true); window_->setSetting("save", "copyAfterSave", true);
        QTRY_VERIFY(!apply_->busy());
        QTest::keyClick(pin, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        const auto urls = QApplication::clipboard()->mimeData()->urls(); QCOMPARE(urls.size(), 1);
        const QString copied = urls.first().toLocalFile();
        const auto cleanup = qScopeGuard([&] { QApplication::clipboard()->clear(); QDir(QFileInfo(copied).absolutePath()).removeRecursively(); });
        QVERIFY(!QImage(copied).isNull()); QVERIFY(!copied.startsWith(directory_.filePath("exports")));
        QVERIFY(QFileInfo(copied).fileName() == "live.jpg");
        window_->setSetting("save", "copyAsFile", false); QTRY_VERIFY(!apply_->busy());
        quick->trigger(); QVERIFY(QApplication::clipboard()->mimeData()->hasImage());
        QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
        window_->setPage(3); window_->refreshHistory(); QTRY_VERIFY(!window_->historyItems().isEmpty());
        editor->close(); pin->close();
        window_->setSetting("save", "dir", ""); window_->setSetting("save", "nameTemplate", "still_{date}_{time}_{ms}");
        window_->setSetting("save", "format", "png"); QTRY_VERIFY(!apply_->busy()); window_->setPage(1);
        window_->setSetting("save", "copyAfterSave", false); QTRY_VERIFY(!apply_->busy());
    }
    void savePreferencesSnapshots_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme"); QTest::addColumn<QSize>("size"); QTest::addColumn<QString>("format");
        QTest::newRow("normal-light-zh") << QString("zh_CN") << QString("light") << QSize(1000, 740) << QString("png");
        QTest::newRow("compact-light-en") << QString("en") << QString("light") << QSize(800, 620) << QString("jpeg");
        QTest::newRow("normal-dark-en") << QString("en") << QString("dark") << QSize(1000, 740) << QString("png");
        QTest::newRow("compact-dark-zh") << QString("zh_CN") << QString("dark") << QSize(800, 620) << QString("jpeg");
    }
    void savePreferencesSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size); QFETCH(QString, format);
        const auto previous = window_->form().value("appearance").toMap(); const auto previousSize = window_->size();
        window_->setSetting("appearance", "language", language); window_->setSetting("appearance", "theme", theme);
        window_->setSetting("save", "format", format); QTRY_VERIFY(!apply_->busy());
        window_->setSetting("save", "copyAsFile", format == "jpeg"); QTRY_VERIFY(!apply_->busy());
        window_->setPage(1); window_->resize(size); QTest::qWait(80);
        auto *flickable = item("SettingsPage")->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        flickable->setProperty("contentY", flickable->property("contentHeight").toReal() - flickable->height());
        QTest::qWait(40);
        for (const auto &name : {"save_dir", "save_nameTemplate", "save_format", "save_copyAsFile", format == "png" ? "save_pngCompression" : "save_jpegQuality"}) {
            auto *control = item(name); QVERIFY(control); QVERIFY(control->isVisible());
            const QRectF bounds(control->mapToScene(QPointF()), control->size());
            QVERIFY2(QRectF(QPointF(), window_->view()->size()).contains(bounds), qPrintable(QString(name)));
        }
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("save-" + QString::fromLatin1(QTest::currentDataTag()))));
        window_->setSetting("appearance", "language", previous.value("language")); window_->setSetting("appearance", "theme", previous.value("theme"));
        window_->setSetting("save", "format", "png"); QTRY_VERIFY(!apply_->busy()); window_->resize(previousSize);
        window_->setSetting("save", "copyAsFile", false); QTRY_VERIFY(!apply_->busy());
    }
    void pinRecoveryPreferencesSnapshots_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme"); QTest::addColumn<QSize>("size");
        QTest::newRow("normal-light-zh") << QString("zh_CN") << QString("light") << QSize(1000, 740);
        QTest::newRow("compact-light-en") << QString("en") << QString("light") << QSize(800, 620);
        QTest::newRow("normal-dark-en") << QString("en") << QString("dark") << QSize(1000, 740);
        QTest::newRow("compact-dark-zh") << QString("zh_CN") << QString("dark") << QSize(800, 620);
    }
    void pinRecoveryPreferencesSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto previousSize = window_->size();
        const bool previous = window_->form().value("pin").toMap().value("restoreOnStartup").toBool();
        window_->setSetting("appearance", "language", language); window_->setSetting("appearance", "theme", theme);
        const bool initial = size.width() < 900; window_->setSetting("pin", "restoreOnStartup", initial); QTRY_VERIFY(!apply_->busy());
        window_->setPage(1); window_->resize(size); QTest::qWait(50);
        auto *control = item("pin_restoreOnStartup"); QVERIFY(control);
        auto *flick = item("SettingsPage")->property("contentItem").value<QQuickItem *>(); QVERIFY(flick);
        const qreal target = flick->property("contentY").toReal() + control->mapToScene(QPointF()).y() - window_->view()->height() / 2.0;
        flick->setProperty("contentY", qBound(0.0, target, flick->property("contentHeight").toReal() - flick->height()));
        control->forceActiveFocus(); QTest::keyClick(window_->view(), Qt::Key_Space); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("pin").toObject().value("restoreOnStartup").toBool(), !initial);
        QTest::qWait(40); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("pin-recovery-" + QString::fromLatin1(QTest::currentDataTag()))));
        window_->setSetting("pin", "restoreOnStartup", previous);
        window_->setSetting("appearance", "language", appearance.value("language")); window_->setSetting("appearance", "theme", appearance.value("theme"));
        QTRY_VERIFY(!apply_->busy()); window_->resize(previousSize);
    }
    void pinRecoveryMenuSnapshots_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme");
        QTest::newRow("light-zh") << QString("zh_CN") << QString("light");
        QTest::newRow("light-en") << QString("en") << QString("light");
        QTest::newRow("dark-zh") << QString("zh_CN") << QString("dark");
        QTest::newRow("dark-en") << QString("en") << QString("dark");
    }
    void pinRecoveryMenuSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme);
        const auto appearance = window_->form().value("appearance").toMap();
        window_->setSetting("appearance", "language", language); window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
        QImage image(240, 120, QImage::Format_RGB32); image.fill(Qt::white);
        auto *pin = new Pin(image, QRect(100, 100, 240, 120), {}, "en-US"); pin->setRestoreAvailable(true); pin->show();
        const auto cleanup = qScopeGuard([&] { delete pin; });
        QSignalSpy requested(pin, &Pin::restoreClosedRequested);
        bool captured = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            auto *menu = pin->findChild<QMenu *>(); if (!menu || !menu->isVisible()) return;
            dismiss.stop(); const auto close = qScopeGuard([&] { menu->close(); });
            auto *restore = menu->findChild<QAction *>("RestoreClosedPin"); QVERIFY(restore); QVERIFY(restore->isEnabled());
            QCOMPARE(menu->actions().size(), 6);
            captured = menu->grab().save(reviewPath("pin-restore-menu-" + language + '-' + theme));
            auto *more = menu->findChild<QMenu *>("PinMoreMenu"); QVERIFY(more);
            menu->setActiveAction(more->menuAction());
            more->popup(menu->mapToGlobal(menu->actionGeometry(more->menuAction()).topRight()));
            more->setActiveAction(restore); QTest::qWait(20);
            captured = captured && more->grab().save(reviewPath("pin-more-menu-" + language + '-' + theme));
            restore->trigger();
            more->close();
        });
        dismiss.start(10);
        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), pin->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(pin, &event);
        QVERIFY(captured); QCOMPARE(requested.count(), 1);
        window_->setSetting("appearance", "language", appearance.value("language")); window_->setSetting("appearance", "theme", appearance.value("theme"));
        QTRY_VERIFY(!apply_->busy());
    }
    void trayMenuSnapshots_data() { pinRecoveryMenuSnapshots_data(); }
    void trayMenuSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme);
        const auto appearance = window_->form().value("appearance").toMap();
        window_->setSetting("appearance", "language", language); window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
        auto *menu = controller_->findChild<QSystemTrayIcon *>()->contextMenu(); QVERIFY(menu);
        QStringList primary;
        for (auto *entry : menu->actions()) if (!entry->isSeparator()) primary.append(entry->text());
        QCOMPARE(primary, QStringList({t("截图并编辑"), t("剪贴板历史…"), t("贴图操作"), t("更多操作"), t("设置…"), t("退出")}));
        auto *pins = menu->findChild<QMenu *>("TrayPinMenu"); auto *more = menu->findChild<QMenu *>("TrayMoreMenu");
        QVERIFY(pins); QVERIFY(more); QCOMPARE(pins->actions().size(), 6); QCOMPARE(more->actions().size(), 7);
        auto *regionPresets = menu->findChild<QAction *>("RegionPresets"); QVERIFY(regionPresets); QVERIFY(more->actions().contains(regionPresets)); QCOMPARE(regionPresets->text(), t("区域预设…"));
        menu->popup(window_->mapToGlobal(QPoint(80, 80))); QTest::qWait(40);
        QVERIFY(menu->grab().save(reviewPath("tray-menu-" + language + '-' + theme)));
        menu->setActiveAction(pins->menuAction()); pins->popup(menu->mapToGlobal(menu->actionGeometry(pins->menuAction()).topRight())); QTest::qWait(30);
        QVERIFY(pins->grab().save(reviewPath("tray-pins-menu-" + language + '-' + theme))); pins->close();
        menu->setActiveAction(more->menuAction()); more->popup(menu->mapToGlobal(menu->actionGeometry(more->menuAction()).topRight())); QTest::qWait(30);
        QVERIFY(more->grab().save(reviewPath("tray-more-menu-" + language + '-' + theme))); more->close(); menu->close();
        window_->setPage(0);
        for (auto *entry : menu->actions()) if (entry->text() == t("设置…")) entry->trigger();
        QCOMPARE(window_->page(), 1);
        window_->setSetting("appearance", "language", appearance.value("language")); window_->setSetting("appearance", "theme", appearance.value("theme")); QTRY_VERIFY(!apply_->busy());
    }
    void titleBarSpaceMatchesPlatform() {
#ifdef Q_OS_WIN
        QVERIFY(window_->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(item("WindowClose")->isVisible());
#else
        QVERIFY(!window_->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!item("WindowClose")->isVisible());
#endif
        for (int page = 0; page < 6; ++page) {
            window_->setPage(page);
            QTest::qWait(20);
            const qreal titleTop = item("PageTitle")->mapToScene(QPointF()).y();
#ifdef Q_OS_WIN
            QVERIFY2(titleTop >= 48, qPrintable(QString::number(titleTop)));
#else
            QVERIFY2(titleTop < 48, qPrintable(QString::number(titleTop)));
#endif
        }
        window_->setPage(0);
    }
    void sidebarMouseKeyboardAndEntryPoints() {
        const QStringList titles{QStringLiteral("剪贴板"), QStringLiteral("通用偏好"), QStringLiteral("快捷键"), QStringLiteral("截图历史"), QStringLiteral("识别与限制"), QStringLiteral("剪贴板设置")};
        for (int page = 0; page < titles.size(); ++page) {
            openPage(page); QCOMPARE(window_->page(), page);
            QCOMPARE(item("PageTitle")->property("text").toString(), titles[page]);
            auto *capture = item("MainCapture"); QVERIFY(capture);
            QCOMPARE(capture->isVisible(), page == 0 || page == 3);
            if (capture->isVisible()) {
                const QRectF captureBounds(capture->mapToScene(QPointF()), capture->size());
                QCOMPARE(captureBounds.bottomRight(), QPointF(window_->view()->width() - 24, window_->view()->height() - 24));
                if (page == 0) {
                    auto *clipboard = item("ClipboardPanel"); QVERIFY(clipboard);
                    QVERIFY(clipboard->mapToScene(QPointF(0, clipboard->height())).y() > captureBounds.top());
                }
            }
            if (page != 0 && page != 3) {
                QVERIFY(item("BackToLibrary")->isVisible());
                QTRY_VERIFY(item("BackToLibrary")->mapToScene(QPointF()).y() < item("MainNavigation")->mapToScene(QPointF()).y());
            }
        }
        controller_->openSettings(); QCOMPARE(window_->page(), 5);
        auto *navigation = item("MainNavigation"); navigation->forceActiveFocus(); window_->setPage(1);
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->page(), 2);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->page(), 1);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->page(), 1);
        click(item("BackToLibrary")); window_->setPage(0); navigation->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->filter(), QString("pinned"));
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->page(), 3);
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->filter(), QString("pinned"));
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->filter(), QString("all"));
        controller_->openClipboard(); QCOMPARE(window_->page(), 0);
        QVERIFY(item("ClipboardSearch")->hasActiveFocus());
        for (int i = 0; i < 30 && !item("ClipboardList")->hasActiveFocus(); ++i) QTest::keyClick(window_->view(), Qt::Key_Tab);
        QVERIFY(item("ClipboardList")->hasActiveFocus());
        QVERIFY(!item("ClipboardSettings"));
        openPage(5); QCOMPARE(window_->page(), 5);
        QVERIFY(item("clipboard_enabled")->isVisible());
        QPointer<MainWindow> closed = window_;
        window_->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(closed.isNull());
        auto *hotkeys = controller_->findChild<Hotkeys *>(); QVERIFY(hotkeys);
        const int clipboardHotkey = hotkeyNames.indexOf("clipboardHistory");
#ifdef Q_OS_WIN
        QCOMPARE(hotkeys->sequence(clipboardHotkey), QKeySequence("Ctrl+Alt+Space"));
#endif
        hotkeys->dispatch(clipboardHotkey);
        locateWindow(); QVERIFY(window_ && window_->isVisible());
        QCOMPARE(window_->page(), 0);
        QVERIFY(item("ClipboardSearch")->hasActiveFocus());
    }
    void mainWindowUsesNormalMacWindow() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native macOS window controls");
        auto *nativeWindow = reinterpret_cast<NSView *>(window_->winId()).window;
        QVERIFY(nativeWindow);
        QCOMPARE(qRound(NSHeight(nativeWindow.contentView.frame)), qRound(NSHeight(nativeWindow.frame)));
        QCOMPARE(window_->view()->geometry().top(), 0);
        QCOMPARE(window_->view()->height(), window_->height());
        QVERIFY(nativeWindow.titlebarAppearsTransparent);
        QCOMPARE(nativeWindow.titleVisibility, NSWindowTitleHidden);
        auto *brand = item("WindowBrand"); QVERIFY(brand && brand->isVisible());
        QVERIFY(brand->mapToScene(QPointF()).y() >= 32);
        QVERIFY(item("MainNavigation")->mapToScene(QPointF()).y() > brand->mapToScene(QPointF(0, brand->height())).y());
        QVERIFY(item("WindowDragArea")->isEnabled());
        for (const auto type : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
            auto *button = [nativeWindow standardWindowButton:type];
            QVERIFY(button.enabled && !button.hidden);
            const NSRect bounds = [button convertRect:button.bounds toView:nativeWindow.contentView];
            QVERIFY(NSContainsRect(nativeWindow.contentView.bounds, bounds));
        }
        QVERIFY2([nativeWindow standardWindowButton:NSWindowMiniaturizeButton].enabled, "The main window minimize button must be enabled");
        QVERIFY(![nativeWindow isKindOfClass:NSPanel.class]);
        QVERIFY(nativeWindow.collectionBehavior & NSWindowCollectionBehaviorFullScreenPrimary);
        if (@available(macOS 13.0, *)) {
            QVERIFY2(nativeWindow.collectionBehavior & NSWindowCollectionBehaviorPrimary, "The main window must have its own application space instead of joining another full-screen app");
            QVERIFY(!(nativeWindow.collectionBehavior & (NSWindowCollectionBehaviorAuxiliary | NSWindowCollectionBehaviorCanJoinAllApplications)));
        }
        QVERIFY(!(nativeWindow.collectionBehavior & (NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenAuxiliary)));
        QCOMPARE(nativeWindow.level, NSInteger(NSNormalWindowLevel));
        const auto appearance = window_->form().value("appearance").toMap();
        for (const auto &theme : {QString("light"), QString("dark")}) {
            window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
            polishSettingsSurface();
            auto *frame = nativeWindow.contentView.superview;
            auto *bitmap = [frame bitmapImageRepForCachingDisplayInRect:frame.bounds]; QVERIFY(bitmap);
            [frame cacheDisplayInRect:frame.bounds toBitmapImageRep:bitmap];
            NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            QVERIFY([png writeToFile:reviewPath("mac-integrated-titlebar-" + theme).toNSString() atomically:YES]);
        }
        window_->setSetting("appearance", "theme", appearance.value("theme")); QTRY_VERIFY(!apply_->busy());
        window_->showMinimized();
        QTRY_VERIFY(nativeWindow.miniaturized);
        controller_->openClipboard();
        QTRY_VERIFY(!nativeWindow.miniaturized);
        QVERIFY(window_->isVisible());
#else
        QSKIP("macOS window behavior");
#endif
    }
    void nativeWindowControls() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Native window state checks require Windows");
        for (const auto &name : {"WindowMinimize", "WindowMaximize", "WindowClose"}) {
            auto *button = item(name); QVERIFY(button);
            auto *content = button->property("contentItem").value<QQuickItem *>();
            QVERIFY(content && !content->childItems().isEmpty());
            auto *icon = content->childItems().first();
            QCOMPARE(icon->mapToItem(button, QPointF(icon->width() / 2, icon->height() / 2)),
                     QPointF(button->width() / 2, button->height() / 2));
        }
        const QSize normal = window_->size();
        click(item("WindowMaximize")); QTRY_VERIFY(window_->isMaximized());
        click(item("WindowMaximize")); QTRY_VERIFY(!window_->isMaximized()); QTRY_COMPARE(window_->size(), normal);
        click(item("WindowMinimize")); QTRY_VERIFY(window_->isMinimized());
        window_->showNormal(); window_->activateWindow(); window_->view()->setFocus(); QTRY_VERIFY(!window_->isMinimized());
    }
    void mainWindowDockFollowsVisibility() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native macOS application policy");
        auto *nativeWindow = reinterpret_cast<NSView *>(window_->winId()).window;
        window_->hide();
        QCOMPARE(NSApp.activationPolicy, NSApplicationActivationPolicyAccessory);
        controller_->openClipboard();
        QCOMPARE(NSApp.activationPolicy, NSApplicationActivationPolicyRegular);
        window_->showMinimized();
        QTRY_VERIFY(nativeWindow.miniaturized);
        QCOMPARE(NSApp.activationPolicy, NSApplicationActivationPolicyRegular);
        controller_->openClipboard();
        QTRY_VERIFY(!nativeWindow.miniaturized);
        window_->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(NSApp.activationPolicy, NSApplicationActivationPolicyAccessory);
        controller_->openSettings(); locateWindow(); QVERIFY(window_);
        QCOMPARE(NSApp.activationPolicy, NSApplicationActivationPolicyRegular);
#else
        QSKIP("macOS Dock behavior");
#endif
    }
    void clipboardSearchFilterPreviewAndNeighborSelection() {
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        for (const auto &text : {QStringLiteral("示例：项目交接要点\n截图、标注与复制操作保持一致。"), QStringLiteral("second sample"), QStringLiteral("https://example.com/design")}) {
            QMimeData mime; mime.setText(text); archive->add(captureClipboard(mime));
        }
        QImage picture(640, 360, QImage::Format_ARGB32); picture.fill(QColor("#dce8fa"));
        QMimeData image; image.setImageData(picture); archive->add(captureClipboard(image));
        QTRY_COMPARE(window_->clips().size(), 4);
        QCOMPARE(item("ClipTitle3")->property("text").toString(), QStringLiteral("示例：项目交接要点"));
        QVERIFY(item("ClipSnippet3")->property("text").toString().contains(QStringLiteral("截图、标注")));
        auto *search = item("ClipboardSearch"); click(search);
        QTest::keyClicks(window_->view(), "second"); QCOMPARE(window_->clips().size(), 1);
        click(item("Filter_image")); QCOMPARE(window_->clips().size(), 0);
        QVERIFY(!item("ClipboardPaste")->isEnabled());
        window_->setSearch({}); QCOMPARE(window_->clips().size(), 1);
        QSignalSpy ready(archive, &ClipboardArchive::ready);
        QTest::qWait(160); QVERIFY(ready.isEmpty());
        for (bool shown : {true, false}) {
            click(item("ClipboardMore"));
            QTRY_VERIFY(item("ClipboardMenuPreview") && item("ClipboardMenuPreview")->isVisible());
            click(item("ClipboardMenuPreview"));
            QTRY_COMPARE(window_->previewVisible(), shown);
            if (shown) QTRY_VERIFY(!window_->previewImage().isEmpty());
        }
        QVERIFY(window_->previewImage().isEmpty());
        window_->setFilter("all"); window_->setSelectedIndex(1);
        for (const auto &theme : {QString("dark"), QString("light")}) {
            window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
            item("ClipboardList")->forceActiveFocus();
            QVERIFY(item("ClipboardList")->hasActiveFocus());
            QVERIFY(item("ClipRow1")->property("color").value<QColor>().alpha() > 0);
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("list-focused-" + theme)));
            click(search); movePointerTo(search);
            QTRY_VERIFY(search->hasActiveFocus());
            QCOMPARE(window_->selectedIndex(), 1);
            QCOMPARE(item("ClipRow1")->property("color").value<QColor>(), QColor(Qt::transparent));
            QVERIFY(!item("Copy1")->isVisible());
            QVERIFY(!item("Delete1")->isVisible());
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("search-focused-" + theme)));
        }
        QTest::keyClick(window_->view(), Qt::Key_Down); QCOMPARE(window_->selectedIndex(), 2);
        QVERIFY(item("ClipboardList")->hasActiveFocus());
        QVERIFY(item("ClipRow2")->property("selected").toBool());
        QTest::keyClick(window_->view(), Qt::Key_Up); QCOMPARE(window_->selectedIndex(), 1);
        click(item("ClipRow1"), Qt::RightButton);
        auto *menu = window_->view()->rootObject()->findChild<QObject *>("ClipboardContextMenu");
        QVERIFY(menu); QTRY_VERIFY(menu->property("visible").toBool());
        QMetaObject::invokeMethod(menu, "close"); QTRY_VERIFY(!menu->property("visible").toBool());
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Space); QVERIFY(window_->previewVisible());
        QTest::keyClick(window_->view(), Qt::Key_Space); QVERIFY(!window_->previewVisible());
        const QString neighbor = window_->clips()[2].toMap().value("id").toString();
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Delete);
        QTRY_COMPARE(window_->clips().size(), 3); QCOMPARE(window_->selectedIndex(), 1);
        QCOMPARE(window_->selected().value("id").toString(), neighbor);
        click(item("Pin" + QString::number(window_->selectedIndex())));
        QTRY_VERIFY(window_->selected().value("pinned").toBool());
        click(item("NavigationPinned")); QCOMPARE(window_->clips().size(), 1);
        window_->setFilter("all");
    }
    void clipboardPreviewTracksLayoutAndVisibility() {
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        const auto count = archive->entries().size();
        QStringList previousIds;
        for (const auto &entry : archive->entries()) previousIds.append(entry.id);
        QImage image(40, 30, QImage::Format_ARGB32); image.fill(QColor(17, 34, 51));
        QMimeData mime; mime.setImageData(image); archive->add(captureClipboard(mime));
        QTRY_COMPARE(archive->entries().size(), count + 1);
        QString id;
        for (const auto &entry : archive->entries()) if (!previousIds.contains(entry.id)) id = entry.id;
        QVERIFY(!id.isEmpty());
        window_->setPage(0); window_->setFilter("image"); window_->setSelectedIndex(0);
        window_->resize(1080, 740); window_->setPreviewVisible(true);
        QTRY_VERIFY(!window_->previewImage().isEmpty());
        window_->resize(800, 740);
        QTRY_VERIFY(window_->previewImage().isEmpty());
        QVERIFY(window_->previewVisible());
        window_->resize(1080, 740); QTRY_VERIFY(!window_->previewImage().isEmpty());
        window_->hide(); QVERIFY(window_->previewImage().isEmpty());
        window_->show(); QTRY_VERIFY(!window_->previewImage().isEmpty());
        window_->setPage(1); QVERIFY(window_->previewImage().isEmpty());
        window_->setPreviewVisible(false); window_->setFilter("all"); archive->remove(id);
        QTRY_COMPARE(archive->entries().size(), count);
        window_->setPage(0); item("ClipboardList")->forceActiveFocus();
    }
    void clipboardHoverActionsStayCenteredAndDeleteHoveredItem() {
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        const int count = window_->clips().size();
        QMimeData mime; mime.setText("Temporary hover sample"); archive->add(captureClipboard(mime));
        QTRY_COMPARE(window_->clips().size(), count + 1);
        const auto clips = window_->clips();
        int hoveredIndex = 0;
        while (hoveredIndex < clips.size() && clips[hoveredIndex].toMap().value("title").toString() != mime.text()) ++hoveredIndex;
        QVERIFY(hoveredIndex < clips.size());
        const int otherIndex = hoveredIndex == 0 ? 1 : 0;
        const QString hoveredId = clips[hoveredIndex].toMap().value("id").toString();
        const auto cleanup = qScopeGuard([&] { archive->remove(hoveredId); });
        const QString suffix = QString::number(hoveredIndex);
        auto *title = item("ClipTitle" + suffix); QVERIFY(title);
        auto *snippet = item("ClipSnippet" + suffix); QVERIFY(snippet);
        auto *time = item("ClipTime" + suffix); QVERIFY(time);
        QVERIFY(qAbs(title->mapToScene(QPointF()).y() - time->mapToScene(QPointF()).y()) < 4);
        const qreal snippetWidth = snippet->width();
        QVERIFY(snippetWidth > item("ClipRow" + suffix)->width() - 100);
        movePointerTo(item("ClipboardSearch"));
        window_->setSelectedIndex(hoveredIndex);
        for (const auto &name : {"Copy", "Pin", "Delete"}) {
            auto *button = item(name + suffix); QVERIFY(button); QTRY_VERIFY(button->isVisible());
        }
        window_->setSelectedIndex(otherIndex);
        QVERIFY(hoveredId != window_->selected().value("id").toString());
        QTRY_VERIFY(!item("Copy" + suffix)->isVisible() && !item("Delete" + suffix)->isVisible());
        auto center = [](QQuickItem *control) { return QPointF(control->width() / 2, control->height() / 2); };
        auto *row = item("ClipRow" + suffix); QVERIFY(row);
        movePointerTo(row);
        QCOMPARE(snippet->width(), snippetWidth);
        for (const auto &name : {"Copy", "Pin", "Delete"}) {
            auto *button = item(name + suffix); QVERIFY(button); QTRY_VERIFY(button->isVisible());
            movePointerTo(button);
            QTRY_VERIFY(button->isVisible() && button->property("hovered").toBool());
            auto *content = button->property("contentItem").value<QQuickItem *>();
            QVERIFY(content && !content->childItems().isEmpty());
            auto *icon = content->childItems().first();
            QTRY_COMPARE(icon->mapToItem(button, center(icon)), center(button));
        }
        if (QGuiApplication::platformName() == "offscreen") {
            click(item("Copy" + suffix));
            QTRY_COMPARE(QGuiApplication::clipboard()->text(), mime.text());
            QGuiApplication::clipboard()->clear();
            window_->setSelectedIndex(otherIndex);
        }
        click(item("Delete" + suffix));
        QTRY_COMPARE(window_->clips().size(), count);
        for (const auto &entry : window_->clips()) QVERIFY(entry.toMap().value("id").toString() != hoveredId);
    }
    void qmlCopyShortcutAndReliablePaste() {
        if (QGuiApplication::platformName() != "offscreen") QSKIP("Clipboard replay uses the isolated offscreen platform; native paste has a separate guarded test");
        window_->setSearch("second"); QTRY_COMPARE(window_->clips().size(), 1);
        QGuiApplication::clipboard()->clear();
        item("ClipboardList")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second sample"));
        QVERIFY(window_->isVisible());
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(window_->view(), Qt::Key_Tab);
        QTest::keyClick(window_->view(), Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second sample"));
        click(item("ClipboardSearch"));
        QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window_->view(), Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), QString("second"));
        window_->hide(); setPasteTargetForTest(42); controller_->openClipboard();
        QVERIFY(window_->pasteDefault());
        QTest::keyClick(window_->view(), Qt::Key_Return);
        QTRY_VERIFY(!window_->isVisible()); QTRY_COMPARE(lastPasteTargetForTest(), quintptr(42));
        controller_->openClipboard(); window_->setSearch({});
        QEvent deactivate(QEvent::WindowDeactivate); QApplication::sendEvent(window_, &deactivate);
        QVERIFY(!window_->pasteDefault());
        QVERIFY(!item("ClipboardPaste")->isVisible());
        QGuiApplication::clipboard()->clear();
    }
    void preferenceClickSavesAndValidates() {
        window_->setPage(1); QTest::qWait(20);
        QVERIFY(!item("SettingsStatus")->isVisible());
        QCOMPARE(item("capture_afterSelectAction")->property("currentText").toString(), QStringLiteral("打开编辑器"));
        auto *magnifier = item("capture_showMagnifier"); QVERIFY(magnifier);
        QSignalSpy finished(apply_, &SettingsApply::finished);
        click(magnifier); QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QVERIFY(!readJson(settingsPath()).value("capture").toObject().value("showMagnifier").toBool());
        QCOMPARE(readJson(settingsPath()).value("future").toObject().value("keep").toInt(), 42);
        window_->setSetting("clipboard", "maxItems", 0);
        QCOMPARE(window_->form().value("clipboard").toMap().value("maxItems").toInt(), 200);
        window_->setSetting("clipboard", "maxItems", 42);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("clipboard").toObject().value("maxItems").toInt(), 42);
        QVERIFY(!item("SettingsStatus")->isVisible());
    }
    void settingsFlickStopsAtBothBounds() {
        window_->setPage(1);
        QTRY_VERIFY(item("SettingsPage"));
        auto *page = item("SettingsPage");
        auto *flickable = page->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
        QTRY_VERIFY(flickable->property("contentHeight").toReal() > flickable->height());
        const qreal top = flickable->property("contentY").toReal();
        QVERIFY(QMetaObject::invokeMethod(flickable, "flick", Q_ARG(qreal, 0.0), Q_ARG(qreal, 1600.0)));
        QTest::qWait(100);
        QCOMPARE(flickable->property("contentY").toReal(), top);
        const qreal bottom = top + flickable->property("contentHeight").toReal() - flickable->height();
        QVERIFY(flickable->setProperty("contentY", bottom));
        QVERIFY(QMetaObject::invokeMethod(flickable, "flick", Q_ARG(qreal, 0.0), Q_ARG(qreal, -1600.0)));
        QTest::qWait(100);
        QCOMPARE(flickable->property("contentY").toReal(), bottom);
    }
    void completedSavePreservesUnfinishedInput() {
        const QString original = window_->form().value("ocr").toMap().value("language").toString();
        window_->setDraft("ocr", "language", "en-US");
        QSignalSpy finished(apply_, &SettingsApply::finished);
        window_->setSetting("clipboard", "enabled", false);
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("en-US"));
        QCOMPARE(apply_->settings().value("ocr").toObject().value("language").toString(), original);
        window_->commitDrafts(); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("ocr").toObject().value("language").toString(), QString("en-US"));
        window_->setSetting("clipboard", "enabled", true); QTRY_VERIFY(!apply_->busy());
    }
    void rejectedHotkeyKeepsPreviousValue() {
        window_->setPage(2); QTest::qWait(20);
        const QString original = apply_->settings().value("hotkeys").toObject().value("capture").toString();
        click(item("hotkey_capture")); QSignalSpy finished(apply_, &SettingsApply::finished);
        QTest::keyClick(window_->view(), Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QCOMPARE(apply_->settings().value("hotkeys").toObject().value("capture").toString(), original);
        QCOMPARE(item("hotkey_capture")->property("text").toString(), QKeySequence(original).toString(QKeySequence::NativeText));
        QVERIFY(window_->statusError());
        QVERIFY(item("SettingsStatus")->isVisible());
    }
    void delayedCaptureOnlyHidesWindowsWhenItStarts() {
        controller_->openClipboard(); locateWindow(); QVERIFY(window_); QTRY_VERIFY(window_->isVisible());
        auto *entry = controller_->findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>("DelayedCapture"); QVERIFY(entry);
        auto delay = []() -> QDialog * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == "CaptureDelayDialog") return dialog; return nullptr; };
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget); result && result->isVisible()) return result; return nullptr; };
        const auto cleanup = qScopeGuard([&] { if (auto *dialog = delay()) dialog->reject(); if (auto *window = overlay()) window->cancelled(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        resetCapturedScreensForTest(); entry->trigger(); QPointer<QDialog> pending = delay(); QVERIFY(pending); QVERIFY(pending->isVisible()); QVERIFY(!pending->isModal());
        auto *seconds = pending->findChild<QSpinBox *>("DelaySeconds"); QVERIFY(seconds); QCOMPARE(seconds->minimum(), 1); QCOMPARE(seconds->maximum(), 60); QCOMPARE(seconds->value(), 5);
        seconds->setValue(2); entry->trigger(); QCOMPARE(delay(), pending.data()); QCOMPARE(seconds->value(), 2); QVERIFY(window_->isVisible()); QCOMPARE(capturedScreenCountForTest(), 0); QVERIFY(!overlay());
        pending->findChild<QPushButton *>("DelayCancel")->click(); QTRY_VERIFY(pending.isNull());
        entry->trigger(); pending = delay(); QVERIFY(pending); pending->findChild<QSpinBox *>("DelaySeconds")->setValue(1); pending->findChild<QPushButton *>("DelayStart")->click();
        auto *countdown = pending->findChild<QLabel *>("DelayCountdown"); QVERIFY(countdown); QVERIFY(countdown->isVisible()); QVERIFY(!pending->findChild<QWidget *>("DelayInputs")->isVisible());
        entry->trigger(); QCOMPARE(delay(), pending.data()); QVERIFY(countdown->isVisible()); QVERIFY(window_->isVisible()); QCOMPARE(capturedScreenCountForTest(), 0); QVERIFY(!overlay());
        pending->findChild<QPushButton *>("DelayCancel")->click(); QTRY_VERIFY(pending.isNull()); QTest::qWait(1200);
        QCOMPARE(capturedScreenCountForTest(), 0); QVERIFY(window_->isVisible()); QVERIFY(!overlay()); QVERIFY(!delay());
        entry->trigger(); pending = delay(); QVERIFY(pending); pending->findChild<QSpinBox *>("DelaySeconds")->setValue(1); pending->findChild<QPushButton *>("DelayStart")->click();
        QVERIFY(window_->isVisible()); QCOMPARE(capturedScreenCountForTest(), 0); QVERIFY(!overlay());
        QTRY_COMPARE_WITH_TIMEOUT(capturedScreenCountForTest(), QGuiApplication::screens().size(), 3000); QTRY_VERIFY(overlay()); QVERIFY(!window_->isVisible()); QTRY_VERIFY(pending.isNull());
        overlay()->cancelled(); QTRY_VERIFY(window_->isVisible()); QTRY_VERIFY(!overlay()); QVERIFY(!delay());
        resetCapturedScreensForTest(); entry->trigger(); pending = delay(); QVERIFY(pending); pending->findChild<QSpinBox *>("DelaySeconds")->setValue(1); pending->findChild<QPushButton *>("DelayStart")->click();
        controller_->capture(); QTRY_VERIFY(pending.isNull()); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000); QCOMPARE(capturedScreenCountForTest(), QGuiApplication::screens().size());
        overlay()->cancelled(); QTRY_VERIFY(window_->isVisible()); QTest::qWait(1200);
        QCOMPARE(capturedScreenCountForTest(), QGuiApplication::screens().size()); QVERIFY(!overlay()); QVERIFY(!delay());
    }
    void destroyingApplicationCancelsPendingCapture() {
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}}));
        auto controller = std::make_unique<Application>(nullptr, false); QTest::qWait(30);
        auto *entry = controller->findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>("DelayedCapture"); QVERIFY(entry); entry->trigger();
        QPointer<QDialog> pending; for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == "CaptureDelayDialog") pending = dialog;
        QVERIFY(pending); pending->findChild<QSpinBox *>("DelaySeconds")->setValue(1); pending->findChild<QPushButton *>("DelayStart")->click();
        resetCapturedScreensForTest(); controller.reset(); QVERIFY(pending.isNull()); QTest::qWait(1200); QCOMPARE(capturedScreenCountForTest(), 0);
        for (auto *widget : QApplication::topLevelWidgets()) QVERIFY(!qobject_cast<Overlay *>(widget));
    }
    void delayedCaptureStaysVisibleWhenTheApplicationIsInactive() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native macOS tool window behavior");
        if (@available(macOS 14.0, *)) {
            auto *entry = controller_->findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>("DelayedCapture"); QVERIFY(entry); entry->trigger();
            QPointer<QDialog> pending;
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == "CaptureDelayDialog") pending = dialog;
            QVERIFY(pending); QProcess target; NSRunningApplication *peer = nil;
            const auto cleanup = qScopeGuard([&] {
                if (target.state() != QProcess::NotRunning) {
                    target.terminate(); if (!target.waitForFinished(500)) { target.kill(); target.waitForFinished(500); }
                }
                [peer release]; [NSApp activate]; if (pending) pending->reject();
            });
            pending->findChild<QSpinBox *>("DelaySeconds")->setValue(60); pending->findChild<QPushButton *>("DelayStart")->click();
            auto *nativeWindow = reinterpret_cast<NSView *>(pending->winId()).window; QVERIFY(nativeWindow); QVERIFY([nativeWindow isKindOfClass:NSPanel.class]);
            QVERIFY2(!static_cast<NSPanel *>(nativeWindow).hidesOnDeactivate, "The cancel button must remain reachable while preparing another application");
            target.setProcessChannelMode(QProcess::MergedChannels); target.start(QCoreApplication::applicationFilePath(), {"--delay-activation-target"});
            QVERIFY2(target.waitForStarted(5000), qPrintable(target.errorString()));
            QTRY_VERIFY_WITH_TIMEOUT((peer = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(target.processId())]) != nil, 15000);
            [peer retain]; [NSApp yieldActivationToApplication:peer];
            QTRY_VERIFY_WITH_TIMEOUT(!NSApp.active && peer.active, 15000);
            QVERIFY(nativeWindow.visible); QVERIFY(pending->isVisible());
        } else QSKIP("Cooperative activation requires macOS 14 or later");
#else
        QSKIP("macOS tool window behavior");
#endif
    }
    void pinsStayVisibleWhenTheApplicationIsInactive() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() != "cocoa") QSKIP("Requires native macOS tool window behavior");
        if (@available(macOS 14.0, *)) {
            QVector<QPointer<PinWindow>> pins; QProcess target; NSRunningApplication *peer = nil;
            const auto cleanup = qScopeGuard([&] {
                if (target.state() != QProcess::NotRunning) {
                    target.terminate(); if (!target.waitForFinished(500)) { target.kill(); target.waitForFinished(500); }
                }
                [peer release]; [NSApp activate]; for (const auto &pin : pins) delete pin.data();
            });
            const QStringList kinds{QStringLiteral("image"), QStringLiteral("plain"), QStringLiteral("html")};
            const QStringList variants{QStringLiteral("new-top"), QStringLiteral("toggled-top"), QStringLiteral("restored-off")};
            QImage image(160, 100, QImage::Format_ARGB32); image.fill(QColor("#dc9a59"));
            const auto origin = QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(60, 80);
            for (int kind = 0; kind < kinds.size(); ++kind) for (int variant = 0; variant < variants.size(); ++variant) {
                const QRect geometry(origin + QPoint(kind * 35, variant * 35), QSize(260, 180)); PinWindow *pin = nullptr;
                if (kind == 0) pin = new Pin(image, geometry, {}, "en-US");
                else if (kind == 1) pin = new TextPin(QStringLiteral("Synthetic plain text\nSelect and copy"));
                else pin = new TextPin(QStringLiteral("Synthetic rich text"), QStringLiteral("<b>Synthetic rich text</b>"));
                pins.append(pin); pin->setObjectName(kinds[kind] + '-' + variants[variant]); pin->setHideOnBlur(false);
                if (variant == 1) { pin->setWindowFlag(Qt::WindowStaysOnTopHint, false); pin->show(); pin->setWindowFlag(Qt::WindowStaysOnTopHint, true); }
                if (variant == 2) { auto state = pin->state(); state.geometry = geometry; state.alwaysOnTop = false; pin->restoreLayout(state); }
                else pin->setGeometry(geometry);
                pin->show();
            }
            auto description = [](PinWindow *pin, NSWindow *nativeWindow) {
                const bool hides = [nativeWindow isKindOfClass:NSPanel.class] && static_cast<NSPanel *>(nativeWindow).hidesOnDeactivate;
                return QString("%1; alwaysOnTop=%2; hidesOnDeactivate=%3; QtVisible=%4").arg(pin->objectName()).arg(bool(pin->windowFlags() & Qt::WindowStaysOnTopHint)).arg(hides).arg(pin->isVisible());
            };
            controller_->openClipboard();
            if (!QTest::qWaitFor([] { return NSApp.active; }, 15000))
                QSKIP("Native activation checks need an unlocked desktop and foreground test app");
            for (const auto &pin : pins) {
                auto *nativeWindow = reinterpret_cast<NSView *>(pin->winId()).window; QVERIFY(nativeWindow); const auto context = description(pin, nativeWindow);
                QVERIFY2(pin->isVisible(), qPrintable(context)); QTRY_VERIFY2_WITH_TIMEOUT(nativeWindow.visible, qPrintable(context), 3000);
            }
            target.setProcessChannelMode(QProcess::MergedChannels); target.start(QCoreApplication::applicationFilePath(), {"--delay-activation-target"});
            QVERIFY2(target.waitForStarted(5000), qPrintable(target.errorString()));
            QTRY_VERIFY_WITH_TIMEOUT((peer = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(target.processId())]) != nil, 15000);
            [peer retain]; [NSApp yieldActivationToApplication:peer]; QTRY_VERIFY_WITH_TIMEOUT(!NSApp.active && peer.active, 15000);
            QTest::qWait(100);
            QVERIFY(!NSApp.active && peer.active);
            QStringList hidden;
            for (const auto &pin : pins) {
                auto *nativeWindow = reinterpret_cast<NSView *>(pin->winId()).window;
                if (!nativeWindow.visible || !pin->isVisible()) hidden.append(description(pin, nativeWindow));
            }
            QVERIFY2(hidden.isEmpty(), qPrintable(hidden.join('\n')));
            for (int kind = 0; kind < kinds.size(); ++kind) QVERIFY(pins[kind * variants.size()]->grab().save(reviewPath("pin-inactive-" + kinds[kind])));
        } else QSKIP("Cooperative activation requires macOS 14 or later");
#else
        QSKIP("macOS tool window behavior");
#endif
    }
    void delayedCaptureSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void delayedCaptureSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto previousSize = window_->size(); const int previousPage = window_->page();
        const auto reset = qScopeGuard([&] { window_->resize(previousSize); window_->setPage(previousPage); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}}); controller_->openClipboard(); window_->resize(size);
        auto *menu = controller_->findChild<QSystemTrayIcon *>()->contextMenu(); auto *more = menu->findChild<QMenu *>("TrayMoreMenu"); auto *entry = menu->findChild<QAction *>("DelayedCapture"); QVERIFY(more); QVERIFY(entry);
        QStringList primary; for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text());
        QCOMPARE(primary, QStringList({t("截图并编辑"), t("剪贴板历史…"), t("贴图操作"), t("更多操作"), t("设置…"), t("退出")})); QCOMPARE(more->actions().size(), 7); QVERIFY(more->actions().contains(entry)); QCOMPARE(entry->text(), t("延时截图…"));
        entry->trigger(); QPointer<QDialog> pending;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && dialog->objectName() == "CaptureDelayDialog") pending = dialog;
        QVERIFY(pending); const auto close = qScopeGuard([&] { if (pending) pending->reject(); more->close(); });
        QTest::qWait(30); const QString suffix = QString::fromLatin1(QTest::currentDataTag()); QVERIFY(pending->grab().save(reviewPath("delay-capture-input-" + suffix)));
        auto *seconds = pending->findChild<QSpinBox *>("DelaySeconds"); QVERIFY(seconds); QCOMPARE(seconds->value(), 5); seconds->setValue(60); pending->findChild<QPushButton *>("DelayStart")->click();
        auto *countdown = pending->findChild<QLabel *>("DelayCountdown"); QVERIFY(countdown); QVERIFY(countdown->isVisible()); QCOMPARE(countdown->text(), t("%1 秒后开始截图").arg(60));
        QTest::qWait(20); QVERIFY(pending->grab().save(reviewPath("delay-capture-countdown-" + suffix))); pending->findChild<QPushButton *>("DelayCancel")->click(); QTRY_VERIFY(pending.isNull());
        QTimer dismiss; dismiss.setSingleShot(true); connect(&dismiss, &QTimer::timeout, more, &QMenu::close); dismiss.start(1000);
        more->popup(window_->mapToGlobal(QPoint(100, 80))); more->setActiveAction(entry); QTest::qWait(30); QVERIFY(more->grab().save(reviewPath("delay-capture-menu-" + suffix))); dismiss.stop(); more->close();
    }
    void repeatCaptureUsesNewPixelsAndCurrentAction() {
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { setCapturedImageForTest({}); qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}, {"capture", QJsonObject{{"afterSelectAction", "annotate"}}}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
        auto *screen = QGuiApplication::screens().last();
        for (auto *candidate : QGuiApplication::screens()) if (candidate->geometry().left() < 0 || candidate->geometry().top() < 0) { screen = candidate; break; }
        const QRectF region(40.25, 35.5, 130.25, 90.5);
        auto overlay = [&]() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget); result && result->isVisible() && result->geometry() == screen->geometry()) return result; return nullptr; };
        auto editor = [&]() -> Editor * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Editor *>(widget); result && !baseline.contains(result)) return result; return nullptr; };
        auto pins = [&] { QList<Pin *> result; for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget); pin && !baseline.contains(pin)) result.append(pin); return result; };
        const auto cleanup = qScopeGuard([&] { if (auto *window = overlay()) window->cancelled(); delete editor(); for (auto *pin : pins()) delete pin; });
        auto frame = [](const QColor &color) { QImage image(641, 359, QImage::Format_ARGB32); image.fill(color); QPainter painter(&image); painter.fillRect(QRect(12, 12, 30, 30), color.darker()); return image; };
        auto *menu = controller->findChild<QSystemTrayIcon *>()->contextMenu(); auto *repeat = menu->findChild<QAction *>("RepeatCapture"); QVERIFY(repeat); QVERIFY(!repeat->isEnabled());
        auto current = frame(QColor("#3975dc")); setCapturedImageForTest(current); controller->capture(); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000); QVERIFY(!repeat->isEnabled());
        overlay()->accepted(cropFrame(current, screen->size(), region), region, "annotate"); QTRY_VERIFY(editor()); QVERIFY(repeat->isEnabled()); delete editor();
        current = frame(QColor("#83bd9b")); setCapturedImageForTest(current); resetCapturedScreensForTest(); repeat->trigger(); QTRY_VERIFY(editor());
        QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(current, screen->size(), region)); QVERIFY(!overlay()); QCOMPARE(capturedScreenCountForTest(), QGuiApplication::screens().size()); delete editor();
        auto *settings = controller->findChild<SettingsApply *>(); QVERIFY(settings);
        settings->apply({{"capture", QJsonObject{{"maxEdge", 320}}}}); QTRY_VERIFY(!settings->busy());
        current = frame(QColor("#b88cbe")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor());
        const auto reduced = current.scaled(320, 179, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(reduced, screen->size(), region)); delete editor();
        settings->apply({{"capture", QJsonObject{{"afterSelectAction", "pin"}, {"maxEdge", 0}}}}); QTRY_VERIFY(!settings->busy());
        current = frame(QColor("#d8a268")); setCapturedImageForTest(current); repeat->trigger(); QTRY_COMPARE(pins().size(), 1); QVERIFY(!overlay()); QCOMPARE(pins().first()->image(), cropFrame(current, screen->size(), region)); delete pins().first();
        settings->apply({{"capture", QJsonObject{{"afterSelectAction", "annotate"}}}}); QTRY_VERIFY(!settings->busy());
        controller->capture(); QTRY_VERIFY(overlay()); overlay()->cancelled(); QTRY_VERIFY(!overlay());
        current = frame(QColor("#df475a")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor()); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(current, screen->size(), region)); delete editor();
        controller->capture("fullScreen"); QTRY_COMPARE(pins().size(), QGuiApplication::screens().size()); for (auto *pin : pins()) { QCOMPARE(pin->image(), current); delete pin; }
        current = frame(QColor("#6eaa90")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor()); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(current, screen->size(), region)); delete editor();
        auto *delayEntry = menu->findChild<QAction *>("DelayedCapture"); QVERIFY(delayEntry); delayEntry->trigger(); QPointer<QDialog> pending;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && !baseline.contains(dialog) && dialog->objectName() == "CaptureDelayDialog") pending = dialog;
        QVERIFY(pending); pending->findChild<QSpinBox *>("DelaySeconds")->setValue(1); pending->findChild<QPushButton *>("DelayStart")->click();
        current = frame(QColor("#517dc6")); setCapturedImageForTest(current); resetCapturedScreensForTest(); repeat->trigger(); QTRY_VERIFY(pending.isNull()); QTRY_VERIFY(editor());
        QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(current, screen->size(), region)); delete editor(); QTest::qWait(1200);
        QCOMPARE(capturedScreenCountForTest(), QGuiApplication::screens().size()); QVERIFY(!overlay()); QVERIFY(!editor());
        controller->capture(); QTRY_VERIFY(overlay()); auto *source = overlay(); QSignalSpy numeric(source, &Overlay::accepted);
        QTest::mousePress(source, Qt::LeftButton, {}, QPoint(40, 60)); QTest::mouseMove(source, QPoint(180, 160)); QTest::mouseRelease(source, Qt::LeftButton, {}, QPoint(180, 160));
        auto *width = source->findChild<QSpinBox *>("CaptureWidth"); auto *height = source->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        width->setValue(401); height->setValue(257); QCOMPARE(QSize(width->value(), height->value()), QSize(401, 257));
        auto *edit = source->findChild<QToolButton *>("captureAction-edit"); QVERIFY(edit); QTest::mouseClick(edit, Qt::LeftButton); QTRY_COMPARE(numeric.size(), 1); QTRY_VERIFY(editor());
        const auto numericRegion = qvariant_cast<QRectF>(numeric.first()[1]); const auto numericPixels = pixelRect(numericRegion, screen->size(), current.size());
        QCOMPARE(numericPixels.size(), QSize(401, 257)); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), current.copy(numericPixels)); delete editor();
        current = frame(QColor("#b0709c")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor()); QVERIFY(!overlay());
        QCOMPARE(editor()->findChild<Canvas *>()->rendered().size(), QSize(401, 257)); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), current.copy(numericPixels)); delete editor();
        controller->capture(); QTRY_VERIFY(overlay()); source = overlay(); QSignalSpy ratioAccepted(source, &Overlay::accepted);
        QTest::mousePress(source, Qt::LeftButton, {}, QPoint(40, 60)); QTest::mouseMove(source, QPoint(180, 160)); QTest::mouseRelease(source, Qt::LeftButton, {}, QPoint(180, 160));
        auto *ratio = source->findChild<QComboBox *>("CaptureAspectRatio"); width = source->findChild<QSpinBox *>("CaptureWidth"); height = source->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(ratio); QVERIFY(width); QVERIFY(height);
        ratio->setCurrentIndex(3); width->setValue(401); QCOMPARE(QSize(width->value(), height->value()), QSize(400, 225));
        edit = source->findChild<QToolButton *>("captureAction-edit"); QVERIFY(edit); QTest::mouseClick(edit, Qt::LeftButton); QTRY_COMPARE(ratioAccepted.size(), 1); QTRY_VERIFY(editor());
        const auto ratioPixels = pixelRect(qvariant_cast<QRectF>(ratioAccepted.first()[1]), screen->size(), current.size()); QCOMPARE(ratioPixels.size(), QSize(400, 225)); delete editor();
        current = frame(QColor("#dc9a59")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor()); QVERIFY(!overlay());
        QCOMPARE(editor()->findChild<Canvas *>()->rendered().size(), QSize(400, 225)); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), current.copy(ratioPixels)); delete editor();
    }
    void repeatCaptureSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void repeatCaptureSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { setCapturedImageForTest({}); qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}, {"capture", QJsonObject{{"afterSelectAction", "annotate"}}}, {"appearance", QJsonObject{{"language", language}, {"theme", theme}}}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard(); MainWindow *library = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = qobject_cast<MainWindow *>(widget); window && !baseline.contains(window)) library = window;
        QVERIFY(library); library->resize(size); auto *screen = QGuiApplication::screens().last();
        auto overlay = [&]() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget); result && result->isVisible() && result->geometry() == screen->geometry()) return result; return nullptr; };
        auto editor = [&]() -> Editor * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Editor *>(widget); result && !baseline.contains(result)) return result; return nullptr; };
        const auto cleanup = qScopeGuard([&] { if (auto *window = overlay()) window->cancelled(); delete editor(); });
        auto *menu = controller->findChild<QSystemTrayIcon *>()->contextMenu(); auto *more = menu->findChild<QMenu *>("TrayMoreMenu"); auto *repeat = menu->findChild<QAction *>("RepeatCapture"); QVERIFY(more); QVERIFY(repeat);
        QStringList primary; for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text());
        QCOMPARE(primary, QStringList({t("截图并编辑"), t("剪贴板历史…"), t("贴图操作"), t("更多操作"), t("设置…"), t("退出")})); QCOMPARE(more->actions().size(), 7); QCOMPARE(repeat->text(), t("重复上次区域")); QVERIFY(!repeat->isEnabled());
        const QString suffix = QString::fromLatin1(QTest::currentDataTag());
        auto saveMenu = [&](const QString &state) {
            const auto close = qScopeGuard([&] { more->close(); }); QTimer dismiss; dismiss.setSingleShot(true); connect(&dismiss, &QTimer::timeout, more, &QMenu::close); dismiss.start(1000);
            more->popup(library->mapToGlobal(QPoint(100, 80))); more->setActiveAction(repeat); QTest::qWait(30); return more->grab().save(reviewPath("repeat-capture-menu-" + state + '-' + suffix));
        };
        QVERIFY(saveMenu("disabled")); QImage image(641, 359, QImage::Format_ARGB32); image.fill(QColor("#83bd9b")); setCapturedImageForTest(image);
        controller->capture(); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000);
        // Simulate a saved region left outside the bounds after the display layout shrinks.
        const QRectF stale(screen->geometry().width() - 20.5, 30.25, 40.75, 80.5);
        overlay()->accepted(image.copy(0, 0, 40, 80), stale, "annotate"); QTRY_VERIFY(editor()); delete editor(); QVERIFY(repeat->isEnabled()); QVERIFY(saveMenu("enabled"));
        bool captured = false, timedOut = false; QString actualTitle, actualText; QTimer dismiss;
#ifdef Q_OS_MACOS
        NSTimer *nativeDismiss = nil; int polls = 0;
        const auto stopNativeTimer = qScopeGuard([&] { [nativeDismiss invalidate]; [nativeDismiss release]; });
        if (QGuiApplication::platformName() == "cocoa") {
            auto poll = [&](NSTimer *timer) {
                if (++polls > 100) { timedOut = true; [timer invalidate]; [NSApp stopModalWithCode:NSAlertFirstButtonReturn]; return; }
                auto *box = library->findChild<QMessageBox *>(); NSWindow *nativeWindow = NSApp.modalWindow; NSView *content = nativeWindow.contentView;
                if (!box || !content) return;
                const auto text = box->text().toUtf8(); NSString *message = [NSString stringWithUTF8String:text.constData()];
                NSMutableArray<NSView *> *views = [NSMutableArray arrayWithObject:content]; bool matches = false;
                while (views.count && !matches) {
                    NSView *view = views.lastObject; [views removeLastObject];
                    if ([view isKindOfClass:NSTextField.class]) matches = [static_cast<NSTextField *>(view).stringValue isEqualToString:message];
                    [views addObjectsFromArray:view.subviews];
                }
                if (!matches) return;
                [timer invalidate]; actualTitle = box->windowTitle(); actualText = box->text();
                const auto close = qScopeGuard([&] {
                    NSView *button = nativeWindow.defaultButtonCell.controlView;
                    if ([button isKindOfClass:NSButton.class]) [static_cast<NSButton *>(button) performClick:nil];
                    else [NSApp stopModalWithCode:NSAlertFirstButtonReturn];
                });
                NSBitmapImageRep *bitmap = [content bitmapImageRepForCachingDisplayInRect:content.bounds];
                if (bitmap) {
                    [content cacheDisplayInRect:content.bounds toBitmapImageRep:bitmap];
                    // Native chrome is transparent here; review content over the system window color.
                    [NSGraphicsContext saveGraphicsState];
                    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap]];
                    [nativeWindow.effectiveAppearance performAsCurrentDrawingAppearance:^{
                        [NSColor.windowBackgroundColor setFill];
                        NSRectFillUsingOperation(NSMakeRect(0, 0, bitmap.pixelsWide, bitmap.pixelsHigh), NSCompositingOperationDestinationOver);
                    }];
                    [NSGraphicsContext restoreGraphicsState];
                    NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                    const auto path = reviewPath("repeat-capture-unavailable-" + suffix).toUtf8();
                    captured = [png writeToFile:[NSString stringWithUTF8String:path.constData()] atomically:YES];
                }
            };
            nativeDismiss = [[NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) { poll(timer); }] retain];
            [NSRunLoop.mainRunLoop addTimer:nativeDismiss forMode:NSRunLoopCommonModes];
            [NSRunLoop.mainRunLoop addTimer:nativeDismiss forMode:NSModalPanelRunLoopMode];
        } else
#endif
        {
            connect(&dismiss, &QTimer::timeout, this, [&] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
                dismiss.stop(); const auto close = qScopeGuard([&] { box->accept(); }); actualTitle = box->windowTitle(); actualText = box->text();
                captured = box->grab().save(reviewPath("repeat-capture-unavailable-" + suffix));
            });
            dismiss.start(10);
        }
        repeat->trigger(); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000); dismiss.stop(); QVERIFY(!timedOut); QVERIFY(captured);
#ifndef Q_OS_MACOS
        QCOMPARE(actualTitle, t("重新选择区域"));
#endif
        QCOMPARE(actualText, t("原屏幕或区域不可用，请重新框选。"));
        QTest::keyClick(overlay(), Qt::Key_Escape); QTRY_VERIFY(!overlay());
        controller->capture(); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000);
        const QRectF valid(32.25, 25.5, 110.25, 70.5); overlay()->accepted(cropFrame(image, screen->size(), valid), valid, "annotate"); QTRY_VERIFY(editor()); delete editor();
        image.fill(QColor("#d8a268")); setCapturedImageForTest(image); repeat->trigger(); QTRY_VERIFY(editor()); QVERIFY(!overlay()); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(image, screen->size(), valid)); delete editor();
    }
    void regionPresetCanReopenBeforeDeferredDeletion() {
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}}));
        QString error; const auto path = directory.filePath("region-presets.json"); QVERIFY2(saveRegionPresets(path, {{"Saved", {"Fixture", "Maker", "Panel", "Serial"}, QRectF(10.25, 20.5, 80.75, 60.25)}}, &error), qPrintable(error));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false);
        auto *entry = controller->findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>("RegionPresets"); QVERIFY(entry); entry->trigger(); QPointer<CapturePresets> first;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<CapturePresets *>(widget); dialog && !baseline.contains(dialog)) first = dialog;
        QVERIFY(first); first->reject(); entry->trigger(); QPointer<CapturePresets> second;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<CapturePresets *>(widget); dialog && dialog != first && !baseline.contains(dialog) && dialog->isVisible()) second = dialog;
        QVERIFY(second); QVERIFY(second != first); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(first.isNull()); QVERIFY(second); QVERIFY(second->isVisible());
        second->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(second.isNull()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
    void regionPresetCaptureUsesNewFramesAndSurvivesRestart() {
        auto *screen = identifiablePresetScreen();
        if (!screen) QSKIP("Persistent preset recall requires a uniquely identifiable system display");
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { setCapturedImageForTest({}); qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}, {"capture", QJsonObject{{"afterSelectAction", "annotate"}}}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native preset capture checks need a foreground test application");
#endif
        auto overlay = [&]() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget); result && result->isVisible() && result->geometry() == screen->geometry()) return result; return nullptr; };
        auto editor = [&]() -> Editor * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Editor *>(widget); result && !baseline.contains(result)) return result; return nullptr; };
        auto manager = [&]() -> CapturePresets * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<CapturePresets *>(widget); result && !baseline.contains(result)) return result; return nullptr; };
        auto pins = [&] { QList<Pin *> result; for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget); pin && !baseline.contains(pin)) result.append(pin); return result; };
        const auto cleanup = qScopeGuard([&] { if (auto *window = overlay()) window->cancelled(); delete editor(); for (auto *pin : pins()) delete pin; });
        auto frame = [](const QColor &color) { QImage image(641, 359, QImage::Format_ARGB32); image.fill(color); QPainter painter(&image); painter.fillRect(QRect(40, 35, 170, 120), color.darker()); return image; };
        auto *menu = controller->findChild<QSystemTrayIcon *>()->contextMenu(); auto *entry = menu->findChild<QAction *>("RegionPresets"); auto *repeat = menu->findChild<QAction *>("RepeatCapture"); QVERIFY(entry); QVERIFY(repeat);
        auto current = frame(QColor("#3975dc")); setCapturedImageForTest(current); controller->capture(); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000);
        auto *source = overlay(); QSignalSpy accepted(source, &Overlay::accepted);
        QTest::mousePress(source, Qt::LeftButton, {}, QPoint(40, 60)); QTest::mouseMove(source, QPoint(180, 160)); QTest::mouseRelease(source, Qt::LeftButton, {}, QPoint(180, 160));
        auto *width = source->findChild<QSpinBox *>("CaptureWidth"); auto *height = source->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height); width->setValue(201); height->setValue(127);
        QTest::mouseClick(source->findChild<QToolButton *>("captureAction-edit"), Qt::LeftButton); QTRY_COMPARE(accepted.size(), 1); QTRY_VERIFY(editor());
        const QRectF region = qvariant_cast<QRectF>(accepted.first()[1]); const QRect pixels = pixelRect(region, screen->size(), current.size()); QCOMPARE(pixels.size(), QSize(201, 127)); delete editor();
        entry->trigger(); QPointer<CapturePresets> panel = manager(); QVERIFY(panel); QVERIFY(panel->isVisible()); QVERIFY(!panel->isModal());
        auto *name = panel->findChild<QLineEdit *>("RegionPresetName"); auto *list = panel->findChild<QListWidget *>("RegionPresetList"); auto *save = panel->findChild<QPushButton *>("SaveRegionPreset"); QVERIFY(name); QVERIFY(list); QVERIFY(save);
        name->setText("Browser area"); QVERIFY(save->isEnabled()); QTest::mouseClick(save, Qt::LeftButton); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Browser area"));
        const auto path = directory.filePath("region-presets.json"); QString error; const auto saved = loadRegionPresets(path, &error); QVERIFY2(saved, qPrintable(error)); QCOMPARE(saved->size(), 1); QCOMPARE(saved->first().region, region);
        const auto identity = presetScreenIdentity(screen); QCOMPARE(saved->first().screen.name, identity.name); QCOMPARE(saved->first().screen.serialNumber, identity.serialNumber);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        auto *settings = controller->findChild<SettingsApply *>(); QVERIFY(settings); QSignalSpy finished(settings, &SettingsApply::finished);
        settings->apply({{"history", QJsonObject{{"dir", directory.filePath("migrated-history")}}}, {"clipboard", QJsonObject{{"dir", directory.filePath("migrated-clipboard")}}}, {"capture", QJsonObject{{"showMagnifier", false}}}});
        QTRY_VERIFY(!finished.isEmpty() && !settings->busy()); QVERIFY2(finished.last()[0].toBool(), qPrintable(finished.last()[3].toString()));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close(); QVERIFY(!QFile::exists(directory.filePath("migrated-history/region-presets.json"))); QVERIFY(!QFile::exists(directory.filePath("migrated-clipboard/region-presets.json")));
        const QRectF other(15.25, 25.5, 70.25, 45.5); controller->capture(); QTRY_VERIFY(overlay()); overlay()->accepted(cropFrame(current, screen->size(), other), other, "annotate"); QTRY_VERIFY(editor()); delete editor();
        entry->trigger(); QCOMPARE(manager(), panel.data()); list->setCurrentRow(0); current = frame(QColor("#83bd9b")); setCapturedImageForTest(current);
        QTest::mouseClick(panel->findChild<QPushButton *>("CaptureRegionPreset"), Qt::LeftButton); QTRY_VERIFY(editor()); QVERIFY(!overlay()); QVERIFY(!panel->isVisible()); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), current.copy(pixels)); delete editor();
        current = frame(QColor("#d8a268")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor()); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), current.copy(pixels)); delete editor();
        controller.reset(); QVERIFY(panel.isNull()); controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
        menu = controller->findChild<QSystemTrayIcon *>()->contextMenu(); entry = menu->findChild<QAction *>("RegionPresets"); QVERIFY(entry); entry->trigger(); panel = manager(); QVERIFY(panel);
        list = panel->findChild<QListWidget *>("RegionPresetList"); QVERIFY(list); QCOMPARE(list->count(), 1); QCOMPARE(list->item(0)->text(), QString("Browser area")); QVERIFY(!panel->findChild<QPushButton *>("SaveRegionPreset")->isEnabled()); list->setCurrentRow(0);
        settings = controller->findChild<SettingsApply *>(); settings->apply({{"capture", QJsonObject{{"afterSelectAction", "pin"}}}}); QTRY_VERIFY(!settings->busy());
        current = frame(QColor("#b0709c")); setCapturedImageForTest(current); QTest::mouseClick(panel->findChild<QPushButton *>("CaptureRegionPreset"), Qt::LeftButton); QTRY_COMPARE(pins().size(), 1); QVERIFY(!overlay()); QVERIFY(!panel->isVisible()); QCOMPARE(pins().first()->image(), current.copy(pixels)); delete pins().first();
        settings->apply({{"capture", QJsonObject{{"afterSelectAction", "annotate"}, {"maxEdge", 320}}}}); QTRY_VERIFY(!settings->busy()); entry->trigger();
        auto *delayEntry = menu->findChild<QAction *>("DelayedCapture"); QVERIFY(delayEntry); delayEntry->trigger(); QPointer<QDialog> pending;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<QDialog *>(widget); dialog && !baseline.contains(dialog) && dialog->objectName() == "CaptureDelayDialog") pending = dialog;
        QVERIFY(pending); pending->findChild<QSpinBox *>("DelaySeconds")->setValue(1); pending->findChild<QPushButton *>("DelayStart")->click();
        current = frame(QColor("#517dc6")); setCapturedImageForTest(current); resetCapturedScreensForTest(); QTest::mouseClick(panel->findChild<QPushButton *>("CaptureRegionPreset"), Qt::LeftButton); QTRY_VERIFY(pending.isNull()); QTRY_VERIFY(editor());
        const auto reduced = current.scaled(320, 179, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(reduced, screen->size(), region)); delete editor(); QTest::qWait(1200);
        QCOMPARE(capturedScreenCountForTest(), QGuiApplication::screens().size()); QVERIFY(!overlay()); QVERIFY(!editor()); QVERIFY(!panel->isVisible()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
    void regionPresetUnavailableFallsBackWithoutReplacingLastRegion_data() {
        QTest::addColumn<QString>("reason"); QTest::newRow("missing-screen") << QString("missing"); QTest::newRow("outside-screen") << QString("outside");
    }
    void regionPresetUnavailableFallsBackWithoutReplacingLastRegion() {
        QFETCH(QString, reason); auto *screen = reason == "outside" ? identifiablePresetScreen() : QGuiApplication::primaryScreen();
        if (!screen) QSKIP("The bounds case requires a uniquely identifiable system display");
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { setCapturedImageForTest({}); qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}, {"capture", QJsonObject{{"afterSelectAction", "annotate"}}}}));
        const QRectF valid(15.25, 25.5, 110.25, 70.5); RegionPreset unavailable{"Unavailable", presetScreenIdentity(screen), valid};
        if (reason == "missing") unavailable.screen.serialNumber = "missing-preset-screen";
        else unavailable.region = QRectF(screen->geometry().width() - 10.5, 25.5, 40.75, 70.5);
        QString error; const auto path = directory.filePath("region-presets.json"); QVERIFY2(saveRegionPresets(path, {unavailable}, &error), qPrintable(error));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard(); MainWindow *library = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = qobject_cast<MainWindow *>(widget); window && !baseline.contains(window)) library = window;
        QVERIFY(library);
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native preset capture checks need a foreground test application");
#endif
        auto overlay = [&]() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget); result && result->isVisible() && result->geometry() == screen->geometry()) return result; return nullptr; };
        auto editor = [&]() -> Editor * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Editor *>(widget); result && !baseline.contains(result)) return result; return nullptr; };
        const auto cleanup = qScopeGuard([&] { if (auto *window = overlay()) window->cancelled(); delete editor(); });
        QImage current(641, 359, QImage::Format_ARGB32); current.fill(QColor("#83bd9b")); setCapturedImageForTest(current); controller->capture(); QTRY_VERIFY(overlay());
        overlay()->accepted(cropFrame(current, screen->size(), valid), valid, "annotate"); QTRY_VERIFY(editor()); delete editor();
        auto *menu = controller->findChild<QSystemTrayIcon *>()->contextMenu(); auto *entry = menu->findChild<QAction *>("RegionPresets"); auto *repeat = menu->findChild<QAction *>("RepeatCapture"); QVERIFY(entry); QVERIFY(repeat); entry->trigger(); CapturePresets *panel = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<CapturePresets *>(widget); dialog && !baseline.contains(dialog)) panel = dialog;
        QVERIFY(panel); auto *list = panel->findChild<QListWidget *>("RegionPresetList"); QVERIFY(list); QCOMPARE(list->count(), 1); list->setCurrentRow(0);
        bool observed = false, timedOut = false; QString message; QTimer dismiss;
#ifdef Q_OS_MACOS
        NSTimer *nativeDismiss = nil; int polls = 0;
        const auto stopNativeTimer = qScopeGuard([&] { [nativeDismiss invalidate]; [nativeDismiss release]; });
        if (QGuiApplication::platformName() == "cocoa") {
            auto poll = [&](NSTimer *timer) {
                if (++polls > 100) { timedOut = true; [timer invalidate]; [NSApp stopModalWithCode:NSAlertFirstButtonReturn]; return; }
                auto *box = library->findChild<QMessageBox *>(); NSWindow *nativeWindow = NSApp.modalWindow; NSView *content = nativeWindow.contentView;
                if (!box || !content || box->text() != t("原屏幕或区域不可用，请重新框选。")) return;
                const auto text = box->text().toUtf8(); NSString *expected = [NSString stringWithUTF8String:text.constData()]; NSMutableArray<NSView *> *views = [NSMutableArray arrayWithObject:content]; bool matches = false;
                while (views.count && !matches) { NSView *view = views.lastObject; [views removeLastObject]; if ([view isKindOfClass:NSTextField.class]) matches = [static_cast<NSTextField *>(view).stringValue isEqualToString:expected]; [views addObjectsFromArray:view.subviews]; }
                if (!matches) return;
                observed = true; message = box->text(); [timer invalidate]; NSView *button = nativeWindow.defaultButtonCell.controlView;
                if ([button isKindOfClass:NSButton.class]) [static_cast<NSButton *>(button) performClick:nil]; else [NSApp stopModalWithCode:NSAlertFirstButtonReturn];
            };
            nativeDismiss = [[NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) { poll(timer); }] retain];
            [NSRunLoop.mainRunLoop addTimer:nativeDismiss forMode:NSRunLoopCommonModes]; [NSRunLoop.mainRunLoop addTimer:nativeDismiss forMode:NSModalPanelRunLoopMode];
        } else
#endif
        {
            connect(&dismiss, &QTimer::timeout, this, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { observed = true; message = box->text(); box->accept(); dismiss.stop(); } }); dismiss.start(10);
        }
        QTest::mouseClick(panel->findChild<QPushButton *>("CaptureRegionPreset"), Qt::LeftButton); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000); dismiss.stop(); QVERIFY(!timedOut); QVERIFY(observed); QCOMPARE(message, t("原屏幕或区域不可用，请重新框选。"));
        QTest::keyClick(overlay(), Qt::Key_Escape); QTRY_VERIFY(!overlay()); current.fill(QColor("#d8a268")); setCapturedImageForTest(current); repeat->trigger(); QTRY_VERIFY(editor()); QCOMPARE(editor()->findChild<Canvas *>()->rendered(), cropFrame(current, screen->size(), valid)); delete editor();
        const auto records = loadRegionPresets(path, &error); QVERIFY(records); QCOMPARE(records->size(), 1); QCOMPARE(records->first().region, unavailable.region); QCOMPARE(records->first().screen.serialNumber, unavailable.screen.serialNumber);
    }
    void regionPresetSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void regionPresetSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}, {"appearance", QJsonObject{{"language", language}, {"theme", theme}}}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard(); MainWindow *library = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *window = qobject_cast<MainWindow *>(widget); window && !baseline.contains(window)) library = window;
        QVERIFY(library); library->resize(size);
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native preset snapshots need a foreground test application");
#endif
        auto *menu = controller->findChild<QSystemTrayIcon *>()->contextMenu(); auto *more = menu->findChild<QMenu *>("TrayMoreMenu"); auto *entry = menu->findChild<QAction *>("RegionPresets"); QVERIFY(more); QVERIFY(entry); QCOMPARE(more->actions().size(), 7); QVERIFY(more->actions().contains(entry)); QCOMPARE(entry->text(), t("区域预设…"));
        QStringList primary; for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text()); QCOMPARE(primary.size(), 6);
        auto manager = [&]() -> CapturePresets * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<CapturePresets *>(widget); result && !baseline.contains(result)) return result; return nullptr; };
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); const QSize panelSize = size.width() > 800 ? QSize(560, 420) : QSize(440, 340);
        entry->trigger(); QPointer<CapturePresets> panel = manager(); QVERIFY(panel); panel->resize(panelSize); QTest::qWait(30);
        auto *list = panel->findChild<QListWidget *>("RegionPresetList"); auto *name = panel->findChild<QLineEdit *>("RegionPresetName"); auto *save = panel->findChild<QPushButton *>("SaveRegionPreset"); auto *capture = panel->findChild<QPushButton *>("CaptureRegionPreset"); auto *remove = panel->findChild<QPushButton *>("DeleteRegionPreset"); QVERIFY(list); QVERIFY(name); QVERIFY(save); QVERIFY(capture); QVERIFY(remove);
        QCOMPARE(list->count(), 0); QVERIFY(!save->isEnabled()); QVERIFY(!capture->isEnabled()); QVERIFY(!remove->isEnabled()); QVERIFY(!panel->isModal()); QVERIFY(panel->grab().save(reviewPath("region-presets-empty-" + suffix)));
        panel->setCurrentRegion({{}, {"Synthetic display", "Maker", "Panel", "Fixture"}, QRectF(40.25, 35.5, 130.25, 90.5)});
        const QString first = language == "en" ? QString("Browser content") : QStringLiteral("浏览器内容"); name->setText(first); QTest::mouseClick(save, Qt::LeftButton); QCOMPARE(list->count(), 1);
        name->setText(QStringLiteral("复制")); QTest::mouseClick(save, Qt::LeftButton); QCOMPARE(list->count(), 2); list->setCurrentRow(0); QCOMPARE(list->item(0)->text(), first); QCOMPARE(list->item(1)->text(), QStringLiteral("复制"));
        QTest::qWait(20); QVERIFY(panel->grab().save(reviewPath("region-presets-saved-" + suffix))); panel->reject(); QTRY_VERIFY(panel.isNull());
        const auto path = directory.filePath("region-presets.json"); QVERIFY(writeJson(path, {{"version", 2}, {"presets", QJsonArray{}}})); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
        entry->trigger(); panel = manager(); QVERIFY(panel); panel->resize(panelSize); list = panel->findChild<QListWidget *>("RegionPresetList"); QVERIFY(list); QCOMPARE(list->count(), 0);
        for (const auto &button : {"SaveRegionPreset", "CaptureRegionPreset", "DeleteRegionPreset"}) QVERIFY(!panel->findChild<QPushButton *>(button)->isEnabled());
        auto *status = panel->findChild<QLabel *>("RegionPresetStatus"); QVERIFY(status); QVERIFY(!status->text().isEmpty()); QTest::qWait(20); QVERIFY(panel->grab().save(reviewPath("region-presets-error-" + suffix))); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
        panel->reject(); QTRY_VERIFY(panel.isNull());
        if (suffix == "normal-light-zh") {
            const auto close = qScopeGuard([&] { more->close(); }); QTimer dismiss; dismiss.setSingleShot(true); connect(&dismiss, &QTimer::timeout, more, &QMenu::close); dismiss.start(1000);
            more->popup(library->mapToGlobal(QPoint(100, 80))); more->setActiveAction(entry); QTest::qWait(30); QVERIFY(more->grab().save(reviewPath("region-presets-menu-" + suffix)));
        }
    }
    void startupUsesSystemStateAndKeepsPreferencesSeparate() {
        const auto languageDraft = window_->form().value("ocr").toMap().value("language");
        const auto reset = qScopeGuard([&] {
            setStartupFailureForTest({}); setStartupStatusForTest(StartupState::Disabled);
            window_->setDraft("ocr", "language", languageDraft); window_->setPage(0); polishSettingsSurface();
        });
        setStartupFailureForTest({}); setStartupStatusForTest(StartupState::Disabled);
        controller_->openClipboard(); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native startup controls need a foreground test application");
#endif
        window_->activateWindow(); QTRY_VERIFY(window_->isActiveWindow()); openPage(1); polishSettingsSurface(); QTRY_VERIFY(!apply_->busy());
        auto *toggle = item("StartupEnabled"); QVERIFY(toggle); QVERIFY(toggle->property("enabled").toBool()); QVERIFY(!toggle->property("checked").toBool());
        const int changes = startupChangeCountForTest(); const auto saved = apply_->settings(), backup = settingsBackupDocument(saved);
        const auto bytes = fixtureBytes(settingsPath()); window_->setDraft("ocr", "language", "unfinished-startup-input");
        click(toggle); QTRY_VERIFY(toggle->property("checked").toBool()); QCOMPARE(startupChangeCountForTest(), changes + 1);
        click(toggle); QTRY_VERIFY(!toggle->property("checked").toBool()); QCOMPARE(startupChangeCountForTest(), changes + 2);
        QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("unfinished-startup-input"));
        QCOMPARE(apply_->settings(), saved); QCOMPARE(settingsBackupDocument(apply_->settings()), backup); QCOMPARE(fixtureBytes(settingsPath()), bytes);

        setStartupFailureForTest("Synthetic startup registration failure"); click(toggle);
        QTRY_VERIFY(!toggle->property("checked").toBool()); QCOMPARE(window_->startup().value("error").toString(), QString("Synthetic startup registration failure"));
        QVERIFY(item("StartupStatus")->isVisible()); QCOMPARE(startupChangeCountForTest(), changes + 3);
        setStartupFailureForTest({});
        QWidget other; other.resize(180, 80); other.show(); other.activateWindow(); QTRY_VERIFY(other.isActiveWindow());
        setStartupStatusForTest(StartupState::Enabled, "Synthetic startup enabled externally"); window_->showAndActivate(); QTRY_VERIFY(window_->isActiveWindow());
        QTRY_VERIFY(toggle->property("checked").toBool()); QVERIFY(window_->startup().value("error").toString().isEmpty());
        QTRY_COMPARE(item("StartupStatus")->property("text").toString(), QString("Synthetic startup enabled externally"));
        other.activateWindow(); QTRY_VERIFY(other.isActiveWindow()); setStartupStatusForTest(StartupState::Disabled); window_->showAndActivate(); QTRY_VERIFY(window_->isActiveWindow());
        QTRY_VERIFY(!toggle->property("checked").toBool()); click(toggle); QTRY_VERIFY(toggle->property("checked").toBool()); QVERIFY(window_->startup().value("error").toString().isEmpty());

        other.activateWindow(); QTRY_VERIFY(other.isActiveWindow());
        setStartupStatusForTest(StartupState::RequiresApproval, "Synthetic startup waiting for system approval"); window_->showAndActivate();
        QTRY_VERIFY(window_->isActiveWindow()); QTRY_VERIFY(!toggle->property("enabled").toBool()); QVERIFY(!toggle->property("checked").toBool());
        QVERIFY(item("StartupSystemSettings")->isVisible()); QVERIFY(item("StartupRemove")->isVisible()); const int opened = startupSettingsOpenedForTest();
        polishSettingsSurface();
        click(item("StartupSystemSettings")); QCOMPARE(startupSettingsOpenedForTest(), opened + 1);
        click(item("StartupRemove")); QTRY_VERIFY(toggle->property("enabled").toBool()); QVERIFY(!toggle->property("checked").toBool());

        setStartupStatusForTest(StartupState::NotFound, "Synthetic startup registration no longer points to this application");
        other.activateWindow(); QTRY_VERIFY(other.isActiveWindow()); window_->showAndActivate(); QTRY_VERIFY(window_->isActiveWindow());
        QTRY_VERIFY(item("StartupRemove")->isVisible()); QVERIFY(!toggle->property("checked").toBool()); QVERIFY(toggle->property("enabled").toBool());
        setStartupFailureForTest("Synthetic missing startup registration failure"); click(toggle);
        QTRY_VERIFY(!toggle->property("checked").toBool()); QCOMPARE(window_->startup().value("error").toString(), QString("Synthetic missing startup registration failure"));
        setStartupFailureForTest({}); click(item("StartupRemove"));
        QTRY_VERIFY(toggle->property("enabled").toBool()); QVERIFY(!toggle->property("checked").toBool());
        setStartupStatusForTest(StartupState::Unavailable, "Synthetic startup unavailable in this build");
        other.activateWindow(); QTRY_VERIFY(other.isActiveWindow()); window_->showAndActivate(); QTRY_VERIFY(window_->isActiveWindow());
        QTRY_VERIFY(!toggle->property("enabled").toBool()); QVERIFY(!item("StartupSystemSettings")->isVisible()); QVERIFY(!item("StartupRemove")->isVisible());
        const auto managerLabel = t("任务计划程序…");
        setStartupStatusForTest(StartupState::Enabled, "Synthetic startup managed by Task Scheduler", managerLabel);
        other.activateWindow(); QTRY_VERIFY(other.isActiveWindow()); window_->showAndActivate(); QTRY_VERIFY(window_->isActiveWindow());
        QTRY_VERIFY(toggle->property("checked").toBool()); QVERIFY(item("StartupSystemSettings")->isVisible()); QVERIFY(!item("StartupRemove")->isVisible());
        QCOMPARE(item("StartupSystemSettings")->property("text").toString(), managerLabel);
        polishSettingsSurface();
        const int managerOpened = startupSettingsOpenedForTest(); click(item("StartupSystemSettings")); QCOMPARE(startupSettingsOpenedForTest(), managerOpened + 1);
        click(toggle); QTRY_VERIFY(!toggle->property("checked").toBool()); QVERIFY(item("StartupSystemSettings")->isVisible());
        QCOMPARE(item("StartupSystemSettings")->property("text").toString(), managerLabel);
        QCOMPARE(apply_->settings(), saved); QCOMPARE(fixtureBytes(settingsPath()), bytes);
    }
    void startupWarningKeepsTheActualStateAndError() {
        const auto reset = qScopeGuard([&] { setStartupFailureForTest({}); setStartupStatusForTest(StartupState::Disabled); window_->setPage(0); });
        setStartupStatusForTest(StartupState::Disabled); window_->setPage(0); window_->setPage(1);
        setStartupFailureForTest("Synthetic task registration warning after state changed", true);
        window_->setStartupEnabled(true);
        QVERIFY(window_->startup().value("enabled").toBool());
        QCOMPARE(window_->startup().value("error").toString(), QString("Synthetic task registration warning after state changed"));
    }
    void startupSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void startupSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = apply_->settings().value("appearance").toObject(); const bool previousDark = window_->dark(); const auto previousSize = window_->size();
        const auto reset = qScopeGuard([&] {
            setStartupFailureForTest({}); setStartupStatusForTest(StartupState::Disabled); window_->setPage(0); polishSettingsSurface();
            window_->setSetting("appearance", "language", appearance.value("language").toVariant()); window_->setSetting("appearance", "theme", appearance.value("theme").toVariant());
            QTRY_COMPARE(apply_->settings().value("appearance").toObject(), appearance); QTRY_VERIFY(!apply_->busy());
            QTRY_COMPARE(InterfacePreferences::instance().english(), appearance.value("language").toString() == "en"); QTRY_COMPARE(window_->dark(), previousDark);
            window_->resize(previousSize); polishSettingsSurface();
        });
        setStartupFailureForTest({}); setStartupStatusForTest(StartupState::Disabled, t("未登记开机启动")); controller_->openClipboard(); polishSettingsSurface(); window_->resize(size); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native startup snapshots need a foreground test application");
#endif
        window_->activateWindow(); QTRY_VERIFY(window_->isActiveWindow());
        window_->setSetting("appearance", "language", language); window_->setSetting("appearance", "theme", theme);
        QTRY_COMPARE(apply_->settings().value("appearance").toObject().value("language").toString(), language);
        QTRY_COMPARE(apply_->settings().value("appearance").toObject().value("theme").toString(), theme); QTRY_VERIFY(!apply_->busy());
        QTRY_COMPARE(InterfacePreferences::instance().english(), language == "en"); QTRY_COMPARE(window_->dark(), theme == "dark");
        openPage(1); polishSettingsSurface(); auto *toggle = item("StartupEnabled"); QVERIFY(toggle); QVERIFY(!toggle->property("checked").toBool());
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("startup-disabled-" + suffix)));
        setStartupFailureForTest("SMAppService: Operation not permitted"); click(toggle); QTRY_VERIFY(!toggle->property("checked").toBool()); QVERIFY(!window_->startup().value("error").toString().isEmpty());
        setStartupFailureForTest({}); click(toggle); QTRY_VERIFY(toggle->property("checked").toBool());
        setStartupStatusForTest(StartupState::RequiresApproval, t("已登记，需在系统设置中允许；当前不会自动启动"));
        window_->setPage(0); openPage(1); polishSettingsSurface(); QTRY_VERIFY(item("StartupSystemSettings")->isVisible()); QVERIFY(!item("StartupEnabled")->property("checked").toBool());
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("startup-approval-" + suffix)));
        setStartupStatusForTest(StartupState::Enabled, t("已启用，由任务计划程序在登录时启动"), t("任务计划程序…"));
        window_->setPage(0); openPage(1); polishSettingsSurface();
        QTRY_VERIFY(item("StartupEnabled")->property("checked").toBool()); QVERIFY(item("StartupSystemSettings")->isVisible()); QVERIFY(!item("StartupRemove")->isVisible());
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("startup-task-enabled-" + suffix)));
    }
    void singleDefaultEditingKeepsOtherDraftsAndUsesKeyboard() {
        const auto tabFocus = QGuiApplication::styleHints()->tabFocusBehavior();
        const auto restoreTabFocus = qScopeGuard([&] { QGuiApplication::styleHints()->setTabFocusBehavior(tabFocus); });
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto previousDirectory = qgetenv("STILL_DATA_DIR"); auto *previousWindow = window_; auto *previousApply = apply_;
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] {
            window_ = previousWindow; apply_ = previousApply; qputenv("STILL_DATA_DIR", previousDirectory);
            InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); polishSettingsSurface();
        });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        const QString originalShortcut = "Ctrl+Alt+Shift+9"; keys["capture"] = originalShortcut;
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"future", QJsonObject{{"keep", 42}}}, {"appearance", QJsonObject::fromVariantMap(appearance)}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
        auto findWindow = [&]() -> MainWindow * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<MainWindow *>(widget); candidate && !baseline.contains(widget)) return candidate; return nullptr; };
        QTRY_VERIFY(findWindow()); window_ = findWindow(); apply_ = controller->findChild<SettingsApply *>(); QVERIFY(apply_); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native single-default editing checks need a foreground test application");
#endif
        window_->activateWindow(); QTRY_VERIFY(window_->isActiveWindow()); polishSettingsSurface();
        openPage(1); auto *text = item("save_nameTemplate"); QVERIFY(text); scrollSettingIntoView(text);
        const QString unfinishedDirectory = directory.filePath("unfinished-save-directory"); window_->setDraft("save", "dir", unfinishedDirectory);
        click(text); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), "typed_{date}");
        QCOMPARE(window_->form().value("save").toMap().value("nameTemplate").toString(), QString("typed_{date}"));
        revealSingleDefault("save", "nameTemplate"); auto *restore = item("reset_save_nameTemplate");
        const auto editorGeometry = QRectF(text->mapToScene(QPointF()), text->size()); QCOMPARE(restore->width(), 36.0);
        click(restore);
        QTRY_COMPARE(window_->form().value("save").toMap().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate").toVariant());
        QTRY_COMPARE(apply_->settings().value("save").toObject().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate")); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(window_->form().value("save").toMap().value("dir").toString(), unfinishedDirectory);
        QCOMPARE(readJson(settingsPath()).value("save").toObject().value("dir").toString(), QString());
        QCOMPARE(QRectF(text->mapToScene(QPointF()), text->size()), editorGeometry); QVERIFY(!restore->property("enabled").toBool()); QVERIFY(!restore->activeFocusOnTab());
        QTest::keyClick(window_->view(), Qt::Key_Tab); QVERIFY(!restore->hasActiveFocus());

        click(text); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), "keyboard_{ms}");
        QTest::keyClick(window_->view(), Qt::Key_Tab); QTRY_VERIFY(restore->hasActiveFocus()); QTRY_VERIFY(restore->opacity() > 0.9);
        QTest::keyClick(window_->view(), Qt::Key_Space);
        QTRY_COMPARE(window_->form().value("save").toMap().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate").toVariant());
        QTRY_COMPARE(apply_->settings().value("save").toObject().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate")); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(window_->form().value("save").toMap().value("dir").toString(), unfinishedDirectory);

        click(text); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), "last_{ms}"); revealSingleDefault("save", "nameTemplate");
        const auto resetPoint = restore->mapToScene(QPointF(restore->width() / 2, restore->height() / 2)).toPoint();
        QTest::mouseClick(window_->view(), Qt::LeftButton, {}, resetPoint); QTest::mouseClick(window_->view(), Qt::LeftButton, {}, resetPoint);
        QTRY_COMPARE(window_->form().value("save").toMap().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate").toVariant());
        QTRY_COMPARE(apply_->settings().value("save").toObject().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate")); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("save").toObject().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate"));
        QCOMPARE(window_->form().value("save").toMap().value("dir").toString(), unfinishedDirectory);
        window_->setDraft("save", "dir", "");

        openPage(2); bool actualDefault = false;
        for (const auto &entry : window_->settingRows()) { const auto row = entry.toMap(); if (row.value("key") == "capture") actualDefault = row.value("defaultValue").toString() == defaultHotkeys().value("capture").toString(); }
        QVERIFY(actualDefault);
        // The shared test application may already own this key; otherwise reserve it here.
        Hotkeys competitor; competitor.bind(0, QKeySequence::fromString(defaultHotkeys().value("capture").toString(), QKeySequence::PortableText));
        revealSingleDefault("hotkeys", "capture"); click(item("reset_hotkeys_capture")); QTRY_VERIFY(!apply_->busy());
        QVERIFY(window_->statusError()); QCOMPARE(window_->form().value("hotkeys").toMap().value("capture").toString(), originalShortcut);
        QCOMPARE(readJson(settingsPath()).value("hotkeys").toObject().value("capture").toString(), originalShortcut);
        QCOMPARE(readJson(settingsPath()).value("future").toObject().value("keep").toInt(), 42);
    }
    void singleDefaultDirectoryAndQuotaConfirmationPreserveData() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto previousDirectory = qgetenv("STILL_DATA_DIR"); auto *previousWindow = window_; auto *previousApply = apply_;
        const auto appearance = window_->form().value("appearance").toMap(); const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto reset = qScopeGuard([&] {
            window_ = previousWindow; apply_ = previousApply; qputenv("STILL_DATA_DIR", previousDirectory);
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); polishSettingsSurface();
        });
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        const QString historyDirectory = directory.filePath("custom-history"), clipboardDirectory = directory.filePath("custom-clips");
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"future", QJsonObject{{"keep", 42}}}, {"appearance", QJsonObject::fromVariantMap(appearance)},
            {"history", QJsonObject{{"dir", historyDirectory}}}, {"clipboard", QJsonObject{{"dir", clipboardDirectory}}}}));
        const QString presets = directory.filePath("region-presets.json"); QVERIFY(writeJson(presets, {{"version", 1}, {"presets", QJsonArray{}}})); const auto presetsBefore = fixtureBytes(presets);
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
        auto findWindow = [&]() -> MainWindow * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<MainWindow *>(widget); candidate && !baseline.contains(widget)) return candidate; return nullptr; };
        QTRY_VERIFY(findWindow()); window_ = findWindow(); apply_ = controller->findChild<SettingsApply *>(); QVERIFY(apply_); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native single-default storage checks need a foreground test application");
#endif
        window_->activateWindow(); QTRY_VERIFY(window_->isActiveWindow()); polishSettingsSurface();
        auto *history = controller->findChild<HistoryStore *>(); auto *archive = controller->findChild<ClipboardArchive *>(); QVERIFY(history); QVERIFY(archive);
        QImage image(30, 20, QImage::Format_ARGB32); image.fill(QColor("#83bd9b")); history->remember(image); QTRY_COMPARE(historyFiles(historyDirectory).size(), 1);
        QMimeData mime; mime.setText("Synthetic clipboard retained during a single-setting reset"); archive->add(captureClipboard(mime)); QTRY_COMPARE(archive->entries().size(), 1);
        const QString historyFile = historyFiles(historyDirectory).first(), clipName = archive->entries().first().id + ".clip";
        const auto historyBefore = fixtureBytes(historyFile), clipBefore = fixtureBytes(QDir(clipboardDirectory).filePath(clipName)); QVERIFY(!historyBefore.isEmpty()); QVERIFY(!clipBefore.isEmpty());
        openPage(3); click(item("HistoryStorage")); QTRY_VERIFY(item("history_dir")); auto *historyInput = item("history_dir"); scrollSettingIntoView(historyInput);
        const QString unfinishedHistory = directory.filePath("unfinished-history"); click(historyInput); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), unfinishedHistory);
        revealSingleDefault("history", "dir"); auto before = apply_->settings(); const auto bytes = fixtureBytes(settingsPath());
        const auto historyNotMigrated = [&] {
            QTRY_VERIFY(!apply_->busy()); QCOMPARE(apply_->settings(), before); QCOMPARE(history->directory(), historyDirectory);
            QCOMPARE(fixtureBytes(historyFile), historyBefore); QVERIFY(historyFiles(unfinishedHistory).isEmpty());
        };
        QVERIFY(confirmSingleDefault(item("reset_history_dir"), QMessageBox::No, {}, historyNotMigrated));
        QCOMPARE(apply_->settings(), before); QCOMPARE(fixtureBytes(settingsPath()), bytes); QCOMPARE(fixtureBytes(historyFile), historyBefore);
        QCOMPARE(historyInput->property("text").toString(), unfinishedHistory); QCOMPARE(window_->form().value("history").toMap().value("dir").toString(), unfinishedHistory);
        revealSingleDefault("history", "dir"); QVERIFY(confirmSingleDefault(item("reset_history_dir"), QMessageBox::Yes, {}, historyNotMigrated)); QTRY_VERIFY(!apply_->busy());
        auto expectedHistory = before.value("history").toObject(); expectedHistory["dir"] = ""; before["history"] = expectedHistory; QCOMPARE(apply_->settings(), before);
        QCOMPARE(history->directory(), directory.filePath("History")); QCOMPARE(fixtureBytes(QDir(history->directory()).filePath(QFileInfo(historyFile).fileName())), historyBefore); QVERIFY(historyFiles(historyDirectory).isEmpty());
        auto *popup = window_->view()->rootObject()->findChild<QObject *>("HistoryStoragePopup"); QVERIFY(popup); QVERIFY(QMetaObject::invokeMethod(popup, "close")); polishSettingsSurface();

        openPage(5); auto *clipboardInput = item("clipboard_dir"); QVERIFY(clipboardInput); scrollSettingIntoView(clipboardInput);
        const QString unfinishedClipboard = directory.filePath("unfinished-clipboard"); click(clipboardInput); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), unfinishedClipboard);
        revealSingleDefault("clipboard", "dir"); before = apply_->settings();
        const auto clipboardNotMigrated = [&] {
            QTRY_VERIFY(!apply_->busy()); QCOMPARE(apply_->settings(), before); QCOMPARE(archive->directory(), clipboardDirectory);
            QCOMPARE(fixtureBytes(QDir(clipboardDirectory).filePath(clipName)), clipBefore); QVERIFY(!QFile::exists(QDir(unfinishedClipboard).filePath(clipName)));
        };
        QVERIFY(confirmSingleDefault(item("reset_clipboard_dir"), QMessageBox::No, {}, clipboardNotMigrated));
        QCOMPARE(apply_->settings(), before); QCOMPARE(fixtureBytes(QDir(clipboardDirectory).filePath(clipName)), clipBefore);
        QCOMPARE(clipboardInput->property("text").toString(), unfinishedClipboard); QCOMPARE(window_->form().value("clipboard").toMap().value("dir").toString(), unfinishedClipboard);
        revealSingleDefault("clipboard", "dir"); QVERIFY(confirmSingleDefault(item("reset_clipboard_dir"), QMessageBox::Yes, {}, clipboardNotMigrated)); QTRY_VERIFY(!apply_->busy());
        auto expectedClipboard = before.value("clipboard").toObject(); expectedClipboard["dir"] = ""; before["clipboard"] = expectedClipboard; QCOMPARE(apply_->settings(), before);
        QTRY_COMPARE(archive->directory(), directory.filePath("Clipboard")); QCOMPARE(fixtureBytes(QDir(archive->directory()).filePath(clipName)), clipBefore); QVERIFY(!QFile::exists(QDir(clipboardDirectory).filePath(clipName)));

        window_->setSetting("clipboard", "maxItems", 650); QTRY_VERIFY(!apply_->busy()); revealSingleDefault("clipboard", "maxItems"); before = apply_->settings();
        QVERIFY(confirmSingleDefault(item("reset_clipboard_maxItems"), QMessageBox::No)); QCOMPARE(apply_->settings(), before);
        revealSingleDefault("clipboard", "maxItems"); QVERIFY(confirmSingleDefault(item("reset_clipboard_maxItems"), QMessageBox::Yes)); QTRY_VERIFY(!apply_->busy()); QCOMPARE(apply_->settings().value("clipboard").toObject().value("maxItems").toInt(), 200);
        window_->setSetting("clipboard", "maxItems", 50); QTRY_VERIFY(!apply_->busy()); revealSingleDefault("clipboard", "maxItems");
        int unexpectedConfirmations = 0; QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] { if (auto *box = window_->findChild<QMessageBox *>("RestoreSettingConfirmation"); box && box->isVisible()) { ++unexpectedConfirmations; box->reject(); } });
        dismiss.start(10); click(item("reset_clipboard_maxItems")); QTRY_VERIFY(!apply_->busy()); dismiss.stop(); QCOMPARE(unexpectedConfirmations, 0);
        QCOMPARE(apply_->settings().value("clipboard").toObject().value("maxItems").toInt(), 200); QCOMPARE(archive->entries().size(), 1);
        auto *defaultRow = item("reset_clipboard_maxItems"); QVERIFY(!defaultRow->property("enabled").toBool()); QVERIFY(!defaultRow->activeFocusOnTab());
        QSignalSpy finished(apply_, &SettingsApply::finished); click(defaultRow); polishSettingsSurface(); QCOMPARE(finished.size(), 0);
        QCOMPARE(fixtureBytes(presets), presetsBefore); QCOMPARE(fixtureBytes(QDir(archive->directory()).filePath(clipName)), clipBefore);
    }
    void singleDefaultQuotaDoesNotCommitRawInputBeforeConfirmation_data() {
        QTest::addColumn<int>("quota"); QTest::addColumn<int>("imageCount"); QTest::addColumn<int>("response"); QTest::addColumn<bool>("commitAfterCancel");
        QTest::newRow("cancel") << 500 << 3 << int(QMessageBox::No) << false;
        QTest::newRow("restore") << 500 << 3 << int(QMessageBox::Yes) << false;
        QTest::newRow("cancel-then-enter") << 500 << 1 << int(QMessageBox::No) << true;
        QTest::newRow("default-with-raw-input") << 200 << 3 << int(QMessageBox::Yes) << false;
    }
    void singleDefaultQuotaDoesNotCommitRawInputBeforeConfirmation() {
        QFETCH(int, quota); QFETCH(int, imageCount); QFETCH(int, response); QFETCH(bool, commitAfterCancel); QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto previousDirectory = qgetenv("STILL_DATA_DIR"); auto *previousWindow = window_; auto *previousApply = apply_;
        const auto appearance = window_->form().value("appearance").toMap(); const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto reset = qScopeGuard([&] {
            window_ = previousWindow; apply_ = previousApply; qputenv("STILL_DATA_DIR", previousDirectory);
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); polishSettingsSurface();
        });
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"appearance", QJsonObject::fromVariantMap(appearance)}, {"history", QJsonObject{{"maxItems", quota}}}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
        auto findWindow = [&]() -> MainWindow * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<MainWindow *>(widget); candidate && !baseline.contains(widget)) return candidate; return nullptr; };
        QTRY_VERIFY(findWindow()); window_ = findWindow(); apply_ = controller->findChild<SettingsApply *>(); QVERIFY(apply_); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native in-progress quota checks need a foreground test application");
#endif
        window_->activateWindow(); QTRY_VERIFY(window_->isActiveWindow()); polishSettingsSurface();
        auto *history = controller->findChild<HistoryStore *>(); QVERIFY(history);
        const QList<QColor> colors{QColor("#83bd9b"), QColor("#3975dc"), QColor("#d8a268")};
        for (int i = 0; i < imageCount; ++i) { QImage image(30, 20, QImage::Format_ARGB32); image.fill(colors[i]); history->remember(image); }
        QTRY_COMPARE(historyFiles(history->directory()).size(), imageCount); const auto files = historyFiles(history->directory()); QVector<QByteArray> contents;
        for (const auto &file : files) { contents.append(fixtureBytes(file)); QVERIFY(!contents.last().isEmpty()); }
        openPage(3); click(item("HistoryStorage")); QTRY_VERIFY(item("history_maxItems"));
        auto *spin = item("history_maxItems"); scrollSettingIntoView(spin); auto *input = spin->property("contentItem").value<QQuickItem *>(); QVERIFY(input);
        if (quota == 200) QVERIFY(!item("reset_history_maxItems")->property("enabled").toBool());
        click(input); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), "1");
        QCOMPARE(input->property("text").toString(), QString("1")); QCOMPARE(window_->form().value("history").toMap().value("maxItems").toInt(), quota); revealSingleDefault("history", "maxItems");
        if (quota == 200) {
            int unexpectedConfirmations = 0; QTimer dismiss;
            connect(&dismiss, &QTimer::timeout, this, [&] { if (auto *box = window_->findChild<QMessageBox *>("RestoreSettingConfirmation"); box && box->isVisible()) { ++unexpectedConfirmations; box->reject(); } });
            dismiss.start(10); click(item("reset_history_maxItems")); QTRY_VERIFY(!apply_->busy()); dismiss.stop(); QCOMPARE(unexpectedConfirmations, 0);
        } else {
            QVERIFY(confirmSingleDefault(item("reset_history_maxItems"), QMessageBox::StandardButton(response), {}, [&] {
                QTRY_VERIFY(!apply_->busy()); QCOMPARE(apply_->settings().value("history").toObject().value("maxItems").toInt(), 500);
                QCOMPARE(historyFiles(history->directory()), files); for (int i = 0; i < files.size(); ++i) QCOMPARE(fixtureBytes(files[i]), contents[i]);
            }));
        }
        QTRY_VERIFY(!apply_->busy());
        if (response == int(QMessageBox::No)) {
            QCOMPARE(apply_->settings().value("history").toObject().value("maxItems").toInt(), 500); QCOMPARE(input->property("text").toString(), QString("1"));
        } else {
            QCOMPARE(apply_->settings().value("history").toObject().value("maxItems").toInt(), 200); QCOMPARE(input->property("text").toString(), QString("200"));
            auto *popup = window_->view()->rootObject()->findChild<QObject *>("HistoryStoragePopup"); QVERIFY(popup); QVERIFY(QMetaObject::invokeMethod(popup, "close")); polishSettingsSurface(); QTRY_VERIFY(!apply_->busy());
            QCOMPARE(apply_->settings().value("history").toObject().value("maxItems").toInt(), 200);
        }
        QCOMPARE(historyFiles(history->directory()), files); for (int i = 0; i < files.size(); ++i) QCOMPARE(fixtureBytes(files[i]), contents[i]);
        if (commitAfterCancel) {
            click(input); QTest::keyClick(window_->view(), Qt::Key_Return); QTRY_VERIFY(!apply_->busy());
            QCOMPARE(apply_->settings().value("history").toObject().value("maxItems").toInt(), 1); QCOMPARE(input->property("text").toString(), QString("1"));
            QCOMPARE(historyFiles(history->directory()), files); QCOMPARE(fixtureBytes(files.first()), contents.first());
        }
    }
    void singleDefaultSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void singleDefaultSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto tabFocus = QGuiApplication::styleHints()->tabFocusBehavior();
        const auto restoreTabFocus = qScopeGuard([&] { QGuiApplication::styleHints()->setTabFocusBehavior(tabFocus); });
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        const auto appearance = apply_->settings().value("appearance").toObject(); const bool previousDark = window_->dark(); const auto previousSize = window_->size();
        const auto savedTemplate = apply_->settings().value("save").toObject().value("nameTemplate").toVariant(); const auto savedQuota = apply_->settings().value("history").toObject().value("maxItems").toVariant();
        const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto reset = qScopeGuard([&] {
            if (auto *popup = window_->view()->rootObject()->findChild<QObject *>("HistoryStoragePopup")) QMetaObject::invokeMethod(popup, "close");
            window_->setPage(0); polishSettingsSurface(); window_->setSetting("save", "nameTemplate", savedTemplate); window_->setSetting("history", "maxItems", savedQuota);
            QTRY_COMPARE(apply_->settings().value("save").toObject().value("nameTemplate").toVariant(), savedTemplate);
            QTRY_COMPARE(apply_->settings().value("history").toObject().value("maxItems").toVariant(), savedQuota); QTRY_VERIFY(!apply_->busy());
            window_->setSetting("appearance", "language", appearance.value("language").toVariant()); window_->setSetting("appearance", "theme", appearance.value("theme").toVariant());
            QTRY_COMPARE(apply_->settings().value("appearance").toObject(), appearance); QTRY_VERIFY(!apply_->busy());
            QTRY_COMPARE(InterfacePreferences::instance().english(), appearance.value("language").toString() == "en"); QTRY_COMPARE(window_->dark(), previousDark);
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); window_->resize(previousSize); polishSettingsSurface();
        });
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); controller_->openClipboard(); polishSettingsSurface();
        window_->resize(size); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native single-default snapshots need a foreground test application");
#endif
        window_->activateWindow(); QTRY_VERIFY(window_->isActiveWindow()); polishSettingsSurface();
        window_->setSetting("appearance", "language", language); window_->setSetting("appearance", "theme", theme);
        QTRY_COMPARE(apply_->settings().value("appearance").toObject().value("language").toString(), language);
        QTRY_COMPARE(apply_->settings().value("appearance").toObject().value("theme").toString(), theme); QTRY_VERIFY(!apply_->busy());
        QTRY_COMPARE(InterfacePreferences::instance().english(), language == "en"); QTRY_COMPARE(window_->dark(), theme == "dark"); polishSettingsSurface();
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); openPage(1); auto *text = item("save_nameTemplate"); QVERIFY(text); scrollSettingIntoView(text);
        click(text); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(window_->view(), "review_{date}"); revealSingleDefault("save", "nameTemplate"); polishSettingsSurface();
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("single-default-hover-" + suffix)));
        QTest::keyClick(window_->view(), Qt::Key_Tab); auto *restore = item("reset_save_nameTemplate"); QTRY_VERIFY(restore->hasActiveFocus()); QTRY_VERIFY(restore->opacity() > 0.9); polishSettingsSurface();
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("single-default-focused-" + suffix)));
        QTest::keyClick(window_->view(), Qt::Key_Space);
        QTRY_COMPARE(window_->form().value("save").toMap().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate").toVariant());
        QTRY_COMPARE(apply_->settings().value("save").toObject().value("nameTemplate"), defaultSettings().value("save").toObject().value("nameTemplate")); QTRY_VERIFY(!apply_->busy());
        window_->setSetting("history", "maxItems", 650); QTRY_VERIFY(!apply_->busy()); openPage(3); click(item("HistoryStorage")); QTRY_VERIFY(item("history_maxItems")); revealSingleDefault("history", "maxItems");
        QVERIFY(confirmSingleDefault(item("reset_history_maxItems"), QMessageBox::No, reviewPath("single-default-confirmation-" + suffix)));
        QCOMPARE(apply_->settings().value("history").toObject().value("maxItems").toInt(), 650);
    }
    void apiSettingsEntryKeepsLocalPreferences() {
        const int previousPage = window_->page();
        const auto restore = qScopeGuard([&] { window_->setPage(previousPage); polishSettingsSurface(); });
        openPage(1); auto *entry = item("ApiSettings"); QVERIFY(entry); QVERIFY(entry->isVisible());
        scrollSettingIntoView(entry);
        const auto form = window_->form(); const auto saved = apply_->settings();
        click(entry); QPointer<QDialog> panel = window_->findChild<QDialog *>("ApiSettingsDialog"); QVERIFY(panel); QTRY_VERIFY(panel->isVisible());
        QCOMPARE(panel->windowModality(), Qt::WindowModal);
        panel->reject(); QTRY_VERIFY(!panel); QCOMPARE(window_->form(), form); QCOMPARE(apply_->settings(), saved);
        window_->setPage(4); QVERIFY(!item("ApiSettings")->isVisible());
    }
    void settingsBackupQmlEntryImportsAndSurvivesRestart() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto previousDirectory = qgetenv("STILL_DATA_DIR"); auto *previousWindow = window_; auto *previousApply = apply_;
        const auto appearance = window_->form().value("appearance").toMap();
        const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto reset = qScopeGuard([&] {
            window_ = previousWindow; apply_ = previousApply; qputenv("STILL_DATA_DIR", previousDirectory);
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance));
        });
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        const QString historyDirectory = directory.filePath("history"), clipboardDirectory = directory.filePath("clips"), exportDirectory = directory.filePath("exports");
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"future", QJsonObject{{"keep", 42}}}, {"history", QJsonObject{{"dir", historyDirectory}}},
            {"clipboard", QJsonObject{{"dir", clipboardDirectory}}}, {"save", QJsonObject{{"dir", exportDirectory}}}}));
        const QString presets = directory.filePath("region-presets.json"); QVERIFY(writeJson(presets, {{"version", 1}, {"presets", QJsonArray{}}}));
        const auto presetsBefore = fixtureBytes(presets); QVERIFY(!presetsBefore.isEmpty());
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard(); QTest::qWait(50);
        auto findWindow = [&]() -> MainWindow * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<MainWindow *>(widget); candidate && !baseline.contains(widget)) return candidate; return nullptr; };
        QTRY_VERIFY(findWindow()); window_ = findWindow(); apply_ = controller->findChild<SettingsApply *>(); QVERIFY(apply_);
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native backup checks need a foreground test application");
#endif
        auto *history = controller->findChild<HistoryStore *>(); auto *archive = controller->findChild<ClipboardArchive *>(); QVERIFY(history); QVERIFY(archive);
        QImage image(30, 20, QImage::Format_ARGB32); image.fill(QColor("#83bd9b")); history->remember(image); QTRY_COMPARE(historyFiles(historyDirectory).size(), 1);
        QMimeData mime; mime.setText("Synthetic clipboard entry retained during settings import"); archive->add(captureClipboard(mime)); QTRY_COMPARE(archive->entries().size(), 1);
        const QString historyFile = historyFiles(historyDirectory).first(), clipIndex = QDir(clipboardDirectory).filePath("index.json");
        const QString clipFile = QDir(clipboardDirectory).filePath(archive->entries().first().id + ".clip");
        const auto historyBefore = fixtureBytes(historyFile), clipsBefore = fixtureBytes(clipIndex), clipBefore = fixtureBytes(clipFile);
        QVERIFY(!historyBefore.isEmpty()); QVERIFY(!clipsBefore.isEmpty()); QVERIFY(!clipBefore.isEmpty());
        auto imported = apply_->settings(); imported["ocr"] = QJsonObject{{"language", "en-US"}};
        imported["appearance"] = QJsonObject{{"language", "en"}, {"theme", "dark"}};
        auto capture = imported.value("capture").toObject(); capture["afterSelectAction"] = "copy"; imported["capture"] = capture;
        for (const auto &group : {"save", "history", "clipboard"}) { auto values = imported.value(group).toObject(); values["dir"] = directory.filePath(QString("other-device/") + group); imported[group] = values; }
        auto save = imported.value("save").toObject(); save["nameTemplate"] = "from_backup_{date}"; imported["save"] = save;
        const QString backup = directory.filePath("import-settings.json"); QString error; QVERIFY2(saveSettingsBackup(backup, imported, &error), qPrintable(error));
        const auto backupBefore = fixtureBytes(backup); const auto before = apply_->settings(); bool handled = false; QTimer driver;
        window_->setDraft("save", "nameTemplate", "unfinished_{ms}"); scrollSettingsBackupIntoView();
        connect(&driver, &QTimer::timeout, this, [&] {
            auto *panel = window_->findChild<QDialog *>("SettingsBackupDialog"); if (!panel || !panel->isVisible()) return;
            driver.stop(); const auto close = qScopeGuard([&] { panel->reject(); }); QTRY_VERIFY(!apply_->busy());
            QVERIFY(panel->isModal()); QVERIFY(confirmSettingsBackup(panel, "RestoreDefaultSettings", QMessageBox::No));
            QCOMPARE(apply_->settings().value("ocr"), before.value("ocr"));
            const QString exported = directory.filePath("export-settings.json");
            QVERIFY(chooseSettingsBackupFile(panel, "ExportSettingsBackup", "SettingsBackupSaveDialog", exported));
            QVERIFY2(QFile::exists(exported), qPrintable(panel->findChild<QLabel *>("SettingsBackupStatus")->text()));
            const auto exportedSettings = parseSettingsBackup(fixtureBytes(exported), &error); QVERIFY2(exportedSettings.has_value(), qPrintable(error));
            QCOMPARE(exportedSettings->value("save").toObject().value("nameTemplate").toString(), QString("unfinished_{ms}"));
            QVERIFY(chooseSettingsBackupFile(panel, "ChooseSettingsBackup", "SettingsBackupOpenDialog", backup));
            auto *directories = panel->findChild<QCheckBox *>("RestoreBackupDirectories"); auto *preview = panel->findChild<QPlainTextEdit *>("SettingsBackupPreview");
            QVERIFY(directories); QVERIFY(preview); QVERIFY(!directories->isChecked()); QVERIFY(preview->isReadOnly()); QVERIFY(preview->toPlainText().contains("en-US"));
            QVERIFY(confirmSettingsBackup(panel, "ImportSettingsBackup", QMessageBox::Yes)); QTRY_VERIFY(!apply_->busy());
            QCOMPARE(window_->page(), 1); QVERIFY(window_->dark()); QVERIFY(InterfacePreferences::instance().english()); polishSettingsSurface();
            QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("en-US"));
            QCOMPARE(window_->form().value("capture").toMap().value("afterSelectAction").toString(), QString("copy"));
            QCOMPARE(window_->form().value("save").toMap().value("nameTemplate").toString(), QString("from_backup_{date}")); handled = true;
        });
        driver.start(10); click(item("SettingsBackup")); QVERIFY(handled); polishSettingsSurface();
        click(item("Navigation4")); QCOMPARE(window_->page(), 4); polishSettingsSurface();
        click(item("Navigation1")); QCOMPARE(window_->page(), 1); polishSettingsSurface();
        window_->commitDrafts(); QTRY_VERIFY(!apply_->busy());
        const auto saved = readJson(settingsPath()); QCOMPARE(saved, apply_->settings()); QCOMPARE(saved.value("future").toObject().value("keep").toInt(), 42);
        QCOMPARE(saved.value("save").toObject().value("dir").toString(), exportDirectory); QCOMPARE(history->directory(), historyDirectory); QCOMPARE(archive->directory(), clipboardDirectory);
        QCOMPARE(fixtureBytes(historyFile), historyBefore); QCOMPARE(fixtureBytes(clipIndex), clipsBefore); QCOMPARE(fixtureBytes(clipFile), clipBefore);
        QCOMPARE(fixtureBytes(presets), presetsBefore); QCOMPARE(fixtureBytes(backup), backupBefore); QVERIFY(!QFile::exists(directory.filePath("other-device")));
        controller.reset(); controller = std::make_unique<Application>(nullptr, false); controller->openSettings(); QTest::qWait(50);
        QTRY_VERIFY(findWindow()); window_ = findWindow(); apply_ = controller->findChild<SettingsApply *>(); QVERIFY(apply_);
        QCOMPARE(window_->form().value("appearance").toMap().value("language").toString(), QString("en"));
        QCOMPARE(window_->form().value("appearance").toMap().value("theme").toString(), QString("dark"));
        QCOMPARE(window_->form().value("ocr").toMap().value("language").toString(), QString("en-US"));
        QCOMPARE(window_->form().value("capture").toMap().value("afterSelectAction").toString(), QString("copy"));
        QCOMPARE(window_->form().value("save").toMap().value("nameTemplate").toString(), QString("from_backup_{date}"));
        QCOMPARE(fixtureBytes(presets), presetsBefore); QCOMPARE(fixtureBytes(historyFile), historyBefore); QCOMPARE(fixtureBytes(clipFile), clipBefore);
    }
    void settingsBackupSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void settingsBackupSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto previousSize = window_->size();
        const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto reset = qScopeGuard([&] {
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs); window_->setPage(0); polishSettingsSurface();
            window_->resize(previousSize); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); polishSettingsSurface();
        });
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); controller_->openClipboard(); polishSettingsSurface();
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}}); polishSettingsSurface();
        window_->resize(size); polishSettingsSurface();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native backup snapshots need a foreground test application");
#endif
        QTemporaryDir directory; QVERIFY(directory.isValid()); auto source = apply_->settings(); auto ocr = source.value("ocr").toObject(); ocr["language"] = "en-US"; source["ocr"] = ocr;
        auto capture = source.value("capture").toObject(); capture["afterSelectAction"] = "copy"; source["capture"] = capture;
        for (const auto &group : {"save", "history", "clipboard"}) { auto values = source.value(group).toObject(); values["dir"] = directory.filePath(QString("other-device/") + group); source[group] = values; }
        const QString backup = directory.filePath("Still-settings.json"); QString error; QVERIFY2(saveSettingsBackup(backup, source, &error), qPrintable(error));
        const auto before = apply_->settings(); const auto bytes = fixtureBytes(settingsPath()); const auto backupBefore = fixtureBytes(backup);
        scrollSettingsBackupIntoView(); const QString suffix = QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("settings-backup-entry-" + suffix)));
        bool handled = false; QTimer driver;
        connect(&driver, &QTimer::timeout, this, [&] {
            auto *panel = window_->findChild<QDialog *>("SettingsBackupDialog"); if (!panel || !panel->isVisible()) return;
            driver.stop(); const auto close = qScopeGuard([&] { panel->reject(); }); panel->resize(size.width() > 800 ? QSize(700, 580) : QSize(560, 500)); QTest::qWait(30);
            QVERIFY(panel->isModal()); QVERIFY(panel->grab().save(reviewPath("settings-backup-initial-" + suffix)));
            QVERIFY(chooseSettingsBackupFile(panel, "ChooseSettingsBackup", "SettingsBackupOpenDialog", backup));
            auto *directories = panel->findChild<QCheckBox *>("RestoreBackupDirectories"); auto *preview = panel->findChild<QPlainTextEdit *>("SettingsBackupPreview");
            QVERIFY(directories); QVERIFY(!directories->isChecked()); QVERIFY(preview); QVERIFY(preview->isReadOnly()); QVERIFY(preview->toPlainText().contains("other-device"));
            QVERIFY(panel->findChild<QPushButton *>("ImportSettingsBackup")->isEnabled()); QTest::qWait(20); QVERIFY(panel->grab().save(reviewPath("settings-backup-preview-" + suffix)));
            panel->activateWindow(); QTRY_VERIFY(panel->isActiveWindow());
            QStyleOptionButton option; option.initFrom(directories); option.text = directories->text();
            const QRect indicator = directories->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, directories);
            QVERIFY(directories->rect().contains(indicator.center())); QTest::mouseClick(directories, Qt::LeftButton, {}, indicator.center());
            QVERIFY(directories->isChecked()); directories->setFocus(); QTRY_VERIFY(directories->hasFocus());
            QVERIFY(preview->toPlainText().contains(t("将按备份恢复目录。"))); QTest::qWait(20);
            QVERIFY(panel->grab().save(reviewPath("settings-backup-directories-" + suffix)));
            QTest::keyClick(directories, Qt::Key_Space); QVERIFY(!directories->isChecked());
            QVERIFY(preview->toPlainText().contains(t("保留本机目录，不使用备份中的路径。")));
            QVERIFY(confirmSettingsBackup(panel, "RestoreDefaultSettings", QMessageBox::No, reviewPath("settings-backup-confirmation-" + suffix)));
            QCOMPARE(apply_->settings(), before); QCOMPARE(fixtureBytes(settingsPath()), bytes); QCOMPARE(fixtureBytes(backup), backupBefore); handled = true;
        });
        driver.start(10); click(item("SettingsBackup")); QVERIFY(handled);
    }
    void ocrCaptureResults_data() {
        QTest::addColumn<QString>("text"); QTest::addColumn<QString>("error");
        QTest::newRow("success") << QStringLiteral("  Screenshot OCR\t保留缩进 😀\r\n\tif (ready) {\r\n    copy();\r\n}\r\n") << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("error") << QStringLiteral("Do not copy a failed result") << QStringLiteral("Synthetic OCR failure");
    }
    void ocrCaptureResults() {
        QFETCH(QString, text); QFETCH(QString, error);
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { setOcrResultForTest({}); setCapturedImageForTest({}); qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8()); QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}}));
        const auto baseline = QApplication::topLevelWidgets(); auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native OCR capture checks need a foreground test application");
#endif
        auto *screen = QGuiApplication::primaryScreen();
        auto overlay = [&]() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget); result && result->isVisible() && result->geometry() == screen->geometry()) return result; return nullptr; };
        auto dialogs = [&] { QList<OcrResultDialog *> results; for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<OcrResultDialog *>(widget); result && !baseline.contains(result)) results.append(result); return results; };
        const auto cleanup = qScopeGuard([&] { if (auto *source = overlay()) source->cancelled(); for (auto *dialog : dialogs()) delete dialog; });
        QImage image(640, 420, QImage::Format_ARGB32); image.fill(QColor("#83bd9b")); setCapturedImageForTest(image);
        const QString marker = QStringLiteral("Keep clipboard until OCR succeeds"); QApplication::clipboard()->setText(marker); setOcrResultForTest({text, {}, error});
        const bool warningExpected = !QSystemTrayIcon::isSystemTrayAvailable() && (!error.isEmpty() || text.isEmpty());
        const QString expectedNotice = error.isEmpty() ? t("未识别到文字") : error; QString shownNotice;
        QTimer dismiss; dismiss.setInterval(10);
        connect(&dismiss, &QTimer::timeout, this, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible() && box->text() == expectedNotice) {
                shownNotice = box->text(); dismiss.stop(); box->accept(); return;
            }
        });
        if (warningExpected) { dismiss.start(); QTimer::singleShot(10000, &dismiss, &QTimer::stop); }
        const int before = ocrCallCountForTest(); controller->capture("ocr"); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000);
        auto *source = overlay(); QSignalSpy accepted(source, &Overlay::accepted);
        QTest::mousePress(source, Qt::LeftButton, {}, QPoint(40, 60)); QTest::mouseMove(source, QPoint(220, 180)); QTest::mouseRelease(source, Qt::LeftButton, {}, QPoint(220, 180)); QTest::keyClick(source, Qt::Key_Return);
        QTRY_COMPARE(accepted.size(), 1); QCOMPARE(accepted.first()[2].toString(), QString("ocr")); QTRY_VERIFY(!overlay());
        QTRY_COMPARE(ocrCallCountForTest(), before + 1); QTRY_VERIFY(controller->findChildren<OcrJob *>().isEmpty());
        if (!error.isEmpty() || text.isEmpty()) { if (warningExpected) QCOMPARE(shownNotice, expectedNotice); else QVERIFY(shownNotice.isEmpty()); QCOMPARE(dialogs().size(), 0); QCOMPARE(QApplication::clipboard()->text(), marker); return; }
        QTRY_COMPARE(dialogs().size(), 1); QCOMPARE(QApplication::clipboard()->text(), text);
        QPointer<OcrResultDialog> first = dialogs().first(); auto *edited = first->findChild<QPlainTextEdit *>("OcrEditedText"); auto *original = first->findChild<QPlainTextEdit *>("OcrOriginalText"); auto *tabs = first->findChild<QTabWidget *>("OcrResultTabs"); auto *copy = first->findChild<QPushButton *>("OcrCopyText"); QVERIFY(edited); QVERIFY(original); QVERIFY(tabs); QVERIFY(copy);
        const QString displayed = QString(text).replace("\r\n", "\n"); QCOMPARE(edited->toPlainText(), displayed); QCOMPARE(original->toPlainText(), displayed); QVERIFY(original->isReadOnly()); QVERIFY(!edited->isReadOnly()); QVERIFY(!first->isModal());
        edited->setFocus(); QTRY_VERIFY(first->isActiveWindow()); QTRY_VERIFY(edited->hasFocus()); edited->selectAll(); QTest::keyClicks(edited, "Corrected OCR text");
        const QString corrected = edited->toPlainText(); QCOMPARE(corrected, QString("Corrected OCR text")); QTest::mouseClick(copy, Qt::LeftButton); QCOMPARE(QApplication::clipboard()->text(), corrected);
        tabs->setCurrentIndex(1); QTest::mouseClick(copy, Qt::LeftButton); QCOMPARE(QApplication::clipboard()->text(), text); tabs->setCurrentIndex(0);
        const QString next = QStringLiteral("Second capture\r\nNew result 😀"); setOcrResultForTest({next, {}, {}}); controller->capture("ocr"); QTRY_VERIFY_WITH_TIMEOUT(overlay(), 3000);
        source = overlay(); QTest::mousePress(source, Qt::LeftButton, {}, QPoint(60, 80)); QTest::mouseMove(source, QPoint(240, 200)); QTest::mouseRelease(source, Qt::LeftButton, {}, QPoint(240, 200)); QTest::keyClick(source, Qt::Key_Return);
        QTRY_COMPARE(dialogs().size(), 2); QTRY_COMPARE(QApplication::clipboard()->text(), next); QCOMPARE(edited->toPlainText(), corrected); QCOMPARE(original->toPlainText(), displayed);
        OcrResultDialog *second = nullptr; for (auto *dialog : dialogs()) if (dialog != first) second = dialog;
        QVERIFY(second); QCOMPARE(second->findChild<QPlainTextEdit *>("OcrOriginalText")->toPlainText(), QString(next).replace("\r\n", "\n"));
        controller.reset(); QTRY_VERIFY(dialogs().isEmpty()); QVERIFY(first.isNull());
    }
    void ocrResultSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void ocrResultSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto previousSize = window_->size();
        const auto reset = qScopeGuard([&] { window_->resize(previousSize); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}}); controller_->openClipboard(); window_->resize(size);
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native OCR result snapshots need a foreground test application");
#endif
        const QString text = QStringLiteral("会议记录  Meeting notes\r\n\r\n项目\t负责人\t状态\r\n截图工具\tLin\t校对中\r\nOCR result\tSam\tReady\r\n\r\nif (ready) {\r\n    copy(text);\r\n}\r\n\r\n原始换行、空格与制表符默认保留。\r\nKeep spacing until you choose to format.\r\nemoji 😀 · 标点 <tag> & text\r\n");
        QPointer<OcrResultDialog> dialog = new OcrResultDialog(text, window_); const auto cleanup = qScopeGuard([&] { delete dialog.data(); }); dialog->resize(size.width() > 800 ? QSize(680, 520) : QSize(520, 420)); dialog->show(); dialog->raise(); dialog->activateWindow(); QTRY_VERIFY(dialog->isActiveWindow());
        auto *edited = dialog->findChild<QPlainTextEdit *>("OcrEditedText"); auto *original = dialog->findChild<QPlainTextEdit *>("OcrOriginalText"); auto *tabs = dialog->findChild<QTabWidget *>("OcrResultTabs"); auto *lineBreaks = dialog->findChild<QComboBox *>("OcrLineBreaks"); auto *spaces = dialog->findChild<QComboBox *>("OcrSpaces"); auto *apply = dialog->findChild<QPushButton *>("OcrApplyFormatting"); auto *copy = dialog->findChild<QPushButton *>("OcrCopyText"); QVERIFY(edited); QVERIFY(original); QVERIFY(tabs); QVERIFY(lineBreaks); QVERIFY(spaces); QVERIFY(apply); QVERIFY(copy);
        QCOMPARE(lineBreaks->currentIndex(), 0); QCOMPARE(spaces->currentIndex(), 0); QCOMPARE(original->toPlainText(), QString(text).replace("\r\n", "\n"));
        edited->setFocus(); QTRY_VERIFY(edited->hasFocus()); auto cursor = edited->textCursor(); cursor.movePosition(QTextCursor::End); edited->setTextCursor(cursor); QTest::keyClicks(edited, "Reviewed / ");
        cursor = edited->textCursor(); cursor.insertText(QStringLiteral("已校对")); edited->setTextCursor(cursor); const QString corrected = edited->toPlainText();
        edited->verticalScrollBar()->setValue(0); const QString suffix = QString::fromLatin1(QTest::currentDataTag()); QTest::qWait(30); QVERIFY(dialog->grab().save(reviewPath("ocr-result-editable-" + suffix)));
        tabs->setCurrentIndex(1); QTest::qWait(20); QVERIFY(dialog->grab().save(reviewPath("ocr-result-original-" + suffix))); QTest::mouseClick(copy, Qt::LeftButton); QCOMPARE(QApplication::clipboard()->text(), text);
        tabs->setCurrentIndex(0); QCOMPARE(edited->toPlainText(), corrected); lineBreaks->setCurrentIndex(1); spaces->setCurrentIndex(1); QTest::mouseClick(apply, Qt::LeftButton);
        QVERIFY(!edited->toPlainText().contains('\n')); QVERIFY(!edited->toPlainText().contains('\t')); QVERIFY(edited->toPlainText().contains(QStringLiteral("已校对"))); QCOMPARE(original->toPlainText(), QString(text).replace("\r\n", "\n"));
        edited->verticalScrollBar()->setValue(0); QTest::qWait(20); QVERIFY(dialog->grab().save(reviewPath("ocr-result-formatted-" + suffix))); QTest::mouseClick(copy, Qt::LeftButton); QCOMPARE(QApplication::clipboard()->text(), edited->toPlainText());
        QTest::keyClick(dialog, Qt::Key_Escape); QTRY_VERIFY(dialog.isNull());
    }
    void ocrResultDragHandoff() {
        if (qEnvironmentVariable("STILL_OCR_DRAG_HANDOFF") != "1" || QGuiApplication::platformName() != "cocoa")
            QSKIP("Explicit Cocoa handoff only; CUA supplies the real drag, not automated CI input");
        controller_->openClipboard();
        if (!QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000))
            QSKIP("Native drag handoff needs a foreground test application");
        const QString originalText = QStringLiteral("Original OCR text"), reviewedText = QStringLiteral("Reviewed OCR drag text");
        QPointer<OcrResultDialog> source = new OcrResultDialog(originalText, window_);
        auto *target = new QPlainTextEdit; const auto cleanup = qScopeGuard([&] { delete source.data(); delete target; });
        target->setObjectName("OcrDragTarget"); target->setWindowTitle("Still OCR drag target"); target->resize(400, 240);
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        source->resize(600, 440); source->move(available.topLeft() + QPoint(20, 40));
        const QPoint targetPosition = available.width() >= 1080 ? available.topLeft() + QPoint(660, 100) : available.topLeft() + QPoint(20, 510);
        target->move(targetPosition); target->show(); source->show(); source->raise(); source->activateWindow();
        auto *edited = source->findChild<QPlainTextEdit *>("OcrEditedText"); auto *original = source->findChild<QPlainTextEdit *>("OcrOriginalText"); auto *tabs = source->findChild<QTabWidget *>("OcrResultTabs"); auto *copy = source->findChild<QPushButton *>("OcrCopyText"); QVERIFY(edited); QVERIFY(original); QVERIFY(tabs); QVERIFY(copy);
        edited->selectAll(); edited->insertPlainText(reviewedText); edited->selectAll(); edited->setFocus(); QTRY_VERIFY(source->isActiveWindow()); QTRY_VERIFY(edited->hasFocus());
        QTest::qWait(50); QVERIFY(!source->frameGeometry().intersects(target->frameGeometry()));
        auto start = edited->textCursor(); start.setPosition(0);
        const QRect sourceViewport(edited->viewport()->mapToGlobal(QPoint()), edited->viewport()->size());
        const QRect targetViewport(target->viewport()->mapToGlobal(QPoint()), target->viewport()->size());
        const QPoint dragStart = edited->viewport()->mapToGlobal(edited->cursorRect(start).center() + QPoint(30, 0));
        qInfo() << "OCR_DRAG_HANDOFF_READY source=" << source->frameGeometry() << "target=" << target->frameGeometry()
                << "selected=" << edited->textCursor().selectedText() << "sourceViewport=" << sourceViewport
                << "targetViewport=" << targetViewport << "dragStart=" << dragStart << "dropPoint=" << targetViewport.center();
        QTRY_COMPARE_WITH_TIMEOUT(target->toPlainText(), reviewedText, 180000);
        if (edited->toPlainText().isEmpty()) { edited->undo(); QCOMPARE(edited->toPlainText(), reviewedText); }
        else QCOMPARE(edited->toPlainText(), reviewedText);
        QCOMPARE(original->toPlainText(), originalText); tabs->setCurrentIndex(1); copy->click();
        QCOMPARE(QApplication::clipboard()->text(), originalText); QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
        tabs->setCurrentIndex(0); QTest::qWait(30); QVERIFY(source->grab().save(reviewPath("ocr-result-drag-source"))); QVERIFY(target->grab().save(reviewPath("ocr-result-drag-target")));
    }
    void captureDimensionsKeepNativeKeyboardUsable() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            controller_->openClipboard();
            if (!QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000))
                QSKIP("Native keyboard/focus checks need a foreground test application");
        }
#endif
        const QSize logical(640, 420); QImage image(960, 630, QImage::Format_ARGB32); image.fill(QColor("#83bd9b"));
        { QPainter painter(&image); painter.fillRect(QRect(150, 100, 450, 260), QColor("#517dc6")); }
        auto *screen = QGuiApplication::primaryScreen(); const QPoint origin = screen->availableGeometry().topLeft() + QPoint(40, 60);
        QList<QPointer<Overlay>> windows; const auto cleanup = qScopeGuard([&] { for (const auto &window : windows) delete window.data(); });
        auto create = [&] {
            QPointer<Overlay> window = new Overlay({image, QRect(origin, logical), {}, screen}, "annotate", false); windows.append(window);
            window->show(); window->raise(); window->activateWindow(); return window;
        };
        auto window = create();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitForWindowActive(window, 15000))
            QSKIP("Native keyboard/focus checks need an unlocked desktop and foreground test app");
#endif
        QTRY_VERIFY(window->isActiveWindow()); QSignalSpy accepted(window, &Overlay::accepted); QSignalSpy cancelled(window, &Overlay::cancelled);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(50, 70)); QTest::mouseMove(window, QPoint(250, 190)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(250, 190));
        auto *width = window->findChild<QSpinBox *>("CaptureWidth"); auto *height = window->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        QVERIFY(!width->keyboardTracking()); QVERIFY(!height->keyboardTracking()); QCOMPARE(QSize(width->value(), height->value()), QSize(300, 180));
        QTest::mouseClick(width->findChild<QLineEdit *>(), Qt::LeftButton); QTest::keyClick(width, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(width, "173");
        QCOMPARE(width->value(), 300); QVERIFY(accepted.isEmpty()); QTest::keyClick(width, Qt::Key_Return); QCOMPARE(width->value(), 173); QVERIFY(accepted.isEmpty()); QVERIFY(window->isVisible());
        QTest::keyClick(width, Qt::Key_Tab); QTRY_VERIFY(height->hasFocus()); QTest::keyClick(height, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(height, "97"); QTest::keyClick(height, Qt::Key_Return);
        QCOMPARE(QSize(width->value(), height->value()), QSize(173, 97)); QVERIFY(accepted.isEmpty());
        QTest::keyClick(height, Qt::Key_P, Qt::ControlModifier); QTest::keyClick(height, Qt::Key_S, Qt::ControlModifier); QVERIFY(accepted.isEmpty());
        QTest::keyClick(height, Qt::Key_Escape); QTRY_VERIFY(window.isNull()); QCOMPARE(cancelled.size(), 1); QVERIFY(accepted.isEmpty());
        window = create(); QTRY_VERIFY(window->isActiveWindow()); QSignalSpy completed(window, &Overlay::accepted);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(550, 350)); QTest::mouseMove(window, QPoint(635, 415)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(635, 415));
        width = window->findChild<QSpinBox *>("CaptureWidth"); height = window->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        QTest::mouseClick(width->findChild<QLineEdit *>(), Qt::LeftButton); QTest::keyClick(width, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(width, "401"); QTest::keyClick(width, Qt::Key_Return);
        QTest::keyClick(width, Qt::Key_Tab); QTRY_VERIFY(height->hasFocus()); QTest::keyClick(height, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(height, "257"); QTest::keyClick(height, Qt::Key_Return);
        QCOMPARE(QSize(width->value(), height->value()), QSize(401, 257)); QVERIFY(completed.isEmpty());
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(500, 330)); QTRY_VERIFY(window->hasFocus()); QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(width->value() < 401); QCOMPARE(height->value(), 257); const QSize expected(width->value(), height->value());
        QTest::keyClick(window, Qt::Key_Return); QTRY_COMPARE(completed.size(), 1);
        const auto output = qvariant_cast<QImage>(completed.first()[0]); const auto region = qvariant_cast<QRectF>(completed.first()[1]);
        QCOMPARE(output.size(), expected); QCOMPARE(output, cropFrame(image, logical, region)); QCOMPARE(completed.first()[2].toString(), QString("edit"));
    }
    void captureDimensionsSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void captureDimensionsSnapshots() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            controller_->openClipboard();
            if (!QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000))
                QSKIP("Native focus snapshots need a foreground test application");
        }
#endif
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QImage image(size * 1.5, QImage::Format_ARGB32); image.fill(QColor("#e5edf7"));
        {
            QPainter painter(&image); QFont heading = painter.font(); heading.setPixelSize(30); painter.setFont(heading); painter.setPen(QColor("#25354d")); painter.drawText(QPoint(80, 90), "Capture layout · 选择截图区域");
            painter.fillRect(QRect(105, 150, 480, 240), QColor("#8ab89d")); painter.fillRect(QRect(660, 180, 330, 270), QColor("#7298cf"));
            QFont body = painter.font(); body.setPixelSize(22); painter.setFont(body); painter.drawText(QPoint(130, 220), "Physical pixels · 精确宽高");
        }
        auto *screen = QGuiApplication::primaryScreen(); const QPoint origin = screen->availableGeometry().topLeft() + QPoint(30, 50);
        auto *window = new Overlay({image, QRect(origin, size), {}, screen}, "annotate", false); const auto cleanup = qScopeGuard([&] { delete window; });
        window->show(); window->raise(); window->activateWindow(); QTest::qWait(40); QSignalSpy accepted(window, &Overlay::accepted);
        QVERIFY(window->grab().save(reviewPath("capture-dimensions-ready-" + QString::fromLatin1(QTest::currentDataTag()))));
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(70, 95)); QTest::mouseMove(window, QPoint(390, 305)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(390, 305));
        auto *width = window->findChild<QSpinBox *>("CaptureWidth"); auto *height = window->findChild<QSpinBox *>("CaptureHeight"); auto *tools = window->findChild<QWidget *>("CaptureTools"); auto *actions = window->findChild<QWidget *>("CaptureActions");
        QVERIFY(width); QVERIFY(height); QVERIFY(tools); QVERIFY(actions); QVERIFY(tools->isVisible());
        QCOMPARE(QSize(width->value(), height->value()), cropFrame(image, size, QRectF(70, 95, 320, 210)).size()); QCOMPARE(width->minimum(), 1); QCOMPARE(height->minimum(), 1); QCOMPARE(width->maximum(), image.width()); QCOMPARE(height->maximum(), image.height());
        QVERIFY2(spinArrowsVisible(width), "Width step arrows must contain visible foreground pixels"); QVERIFY2(spinArrowsVisible(height), "Height step arrows must contain visible foreground pixels");
        const auto buttons = actions->findChildren<QToolButton *>(); QCOMPARE(buttons.size(), 8); for (auto *button : buttons) { QCOMPARE(button->size(), QSize(32, 32)); QCOMPARE(button->iconSize(), QSize(20, 20)); }
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); QTest::qWait(20); QVERIFY(window->grab().save(reviewPath("capture-dimensions-selection-" + suffix)));
        auto focusState = [&] {
            const auto identity = [](QObject *object) { return object ? QString("%1(%2)").arg(QString::fromLatin1(object->metaObject()->className()), object->objectName()) : QStringLiteral("<null>"); };
            return QString("windowActive=%1 applicationState=%2 activeWindow=%3 focusWidget=%4 widthFocusPolicy=%5")
                .arg(window->isActiveWindow()).arg(int(QGuiApplication::applicationState())).arg(identity(QApplication::activeWindow()), identity(QApplication::focusWidget())).arg(int(width->focusPolicy()));
        };
        const int selectedHeight = height->value(); width->setValue(640); width->setFocus(); width->selectAll(); QTRY_VERIFY2(width->hasFocus(), qPrintable(focusState())); QCOMPARE(QSize(width->value(), height->value()), QSize(640, selectedHeight));
        QTest::qWait(20); QVERIFY(tools->grab().save(reviewPath("capture-dimensions-focused-" + suffix)));
        width->setValue(image.width()); height->setValue(image.height()); QCOMPARE(QSize(width->value(), height->value()), image.size());
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(size.width() / 2, size.height() / 2)); QTest::qWait(20); QVERIFY(window->grab().save(reviewPath("capture-dimensions-fullframe-" + suffix)));
        QTest::keyClick(window, Qt::Key_Return); QTRY_COMPARE(accepted.size(), 1); const auto output = qvariant_cast<QImage>(accepted.first()[0]);
        QCOMPARE(output.size(), QSize(width->value(), height->value())); QCOMPARE(output, image);
    }
    void captureDimensionArrowsAtAvailableScreenDpi_data() {
        QTest::addColumn<int>("screenIndex"); const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) QTest::newRow(qPrintable(QString("screen-%1-dpr-%2").arg(i).arg(screens[i]->devicePixelRatio()))) << i;
    }
    void captureDimensionArrowsAtAvailableScreenDpi() {
        controller_->openClipboard();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000))
            QSKIP("Native screen DPI checks need a foreground test application");
#endif
        QFETCH(int, screenIndex); auto *target = QGuiApplication::screens()[screenIndex];
        const auto appearance = window_->form().value("appearance").toMap(); const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", "en"}, {"theme", "dark"}});
        QImage image(900, 600, QImage::Format_ARGB32); image.fill(QColor("#83bd9b")); const QRect geometry(target->availableGeometry().topLeft() + QPoint(40, 60), QSize(600, 400));
        auto *window = new Overlay({image, geometry, {}, target}, "annotate", false); const auto cleanup = qScopeGuard([&] { delete window; });
        window->setScreen(target); window->show(); QTest::qWait(30); window->setScreen(target); window->setGeometry(geometry);
        QTRY_COMPARE(window->screen(), target); QTRY_COMPARE(window->devicePixelRatioF(), target->devicePixelRatio()); QTRY_VERIFY(target->availableGeometry().contains(window->geometry()));
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(50, 60)); QTest::mouseMove(window, QPoint(250, 210)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(250, 210));
        auto *width = window->findChild<QSpinBox *>("CaptureWidth"); auto *height = window->findChild<QSpinBox *>("CaptureHeight"); QVERIFY(width); QVERIFY(height);
        QVERIFY2(spinArrowsVisible(width), "Width step arrows must be visible at the target screen DPR"); QVERIFY2(spinArrowsVisible(height), "Height step arrows must be visible at the target screen DPR");
        auto *ratio = window->findChild<QComboBox *>("CaptureAspectRatio"); QVERIFY(ratio); QVERIFY2(comboArrowVisible(ratio), "Ratio dropdown arrows must be visible at the target screen DPR");
    }
    void captureRatioKeepsNativeKeyboardUsable() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            controller_->openClipboard();
            if (!QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native ratio keyboard checks need a foreground test application");
        }
#endif
        QImage image(960, 630, QImage::Format_ARGB32); image.fill(QColor("#83bd9b")); auto *screen = QGuiApplication::primaryScreen();
        QPointer<Overlay> window = new Overlay({image, QRect(screen->availableGeometry().topLeft() + QPoint(40, 60), QSize(640, 420)), {}, screen}, "annotate", false);
        const auto cleanup = qScopeGuard([&] { delete window.data(); }); window->show(); window->raise(); window->activateWindow(); QTRY_VERIFY(window->isActiveWindow());
        QSignalSpy accepted(window, &Overlay::accepted); QSignalSpy cancelled(window, &Overlay::cancelled);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(50, 70)); QTest::mouseMove(window, QPoint(250, 190)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(250, 190));
        auto *width = window->findChild<QSpinBox *>("CaptureWidth"); auto *height = window->findChild<QSpinBox *>("CaptureHeight"); auto *ratio = window->findChild<QComboBox *>("CaptureAspectRatio"); QVERIFY(width); QVERIFY(height); QVERIFY(ratio);
        QCOMPARE(ratio->count(), 6); QCOMPARE(ratio->currentIndex(), 0); QCOMPARE(ratio->itemData(3).toSize(), QSize(16, 9));
        QTest::mouseClick(width->findChild<QLineEdit *>(), Qt::LeftButton); QTest::keyClick(width, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(width, "401"); QCOMPARE(width->value(), 300);
        QTest::keyClick(width, Qt::Key_Tab); QTRY_VERIFY(height->hasFocus()); QTest::keyClick(height, Qt::Key_Tab);
        if (QGuiApplication::platformName() == "cocoa" && QGuiApplication::styleHints()->tabFocusBehavior() != Qt::TabFocusAllControls) {
            QCOMPARE(ratio->focusPolicy(), Qt::TabFocus); QVERIFY(!ratio->hasFocus()); ratio->setFocus(Qt::OtherFocusReason);
        }
        QTRY_VERIFY(ratio->hasFocus());
        for (int i = 0; i < 3; ++i) QTest::keyClick(ratio, Qt::Key_Down);
        QTest::keyClick(ratio, Qt::Key_Return); QCOMPARE(ratio->currentIndex(), 3); QCOMPARE(QSize(width->value(), height->value()), QSize(400, 225)); QVERIFY(accepted.isEmpty());
        QCOMPARE(width->singleStep(), 16); QCOMPARE(height->singleStep(), 9); QCOMPARE(width->minimum(), 16); QCOMPARE(height->minimum(), 9);
        QTest::mouseClick(width->findChild<QLineEdit *>(), Qt::LeftButton); QTest::keyClick(width, Qt::Key_Up); QCOMPARE(QSize(width->value(), height->value()), QSize(416, 234));
        QTest::keyClick(width, Qt::Key_Tab); QTRY_VERIFY(height->hasFocus()); QTest::keyClick(height, Qt::Key_Down); QCOMPARE(QSize(width->value(), height->value()), QSize(400, 225));
        QTest::keyClick(height, Qt::Key_Tab);
        if (QGuiApplication::platformName() == "cocoa" && QGuiApplication::styleHints()->tabFocusBehavior() != Qt::TabFocusAllControls) {
            QCOMPARE(ratio->focusPolicy(), Qt::TabFocus); QVERIFY(!ratio->hasFocus()); ratio->setFocus(Qt::OtherFocusReason);
        }
        QTRY_VERIFY(ratio->hasFocus()); QTest::keyClick(ratio, Qt::Key_P, Qt::ControlModifier); QTest::keyClick(ratio, Qt::Key_S, Qt::ControlModifier); QVERIFY(accepted.isEmpty());
        QVERIFY2(ratioPopupClosesWithEscape(ratio), "The actual ratio popup must close after Escape"); QVERIFY(window); QVERIFY(window->isVisible()); QVERIFY(accepted.isEmpty()); QVERIFY(cancelled.isEmpty());
        QTest::keyClick(ratio, Qt::Key_Escape); QTRY_VERIFY(window.isNull()); QCOMPARE(cancelled.size(), 1); QVERIFY(accepted.isEmpty());
    }
    void captureRatioSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void captureRatioSnapshots() {
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa") {
            controller_->openClipboard();
            if (!QTest::qWaitFor([] { return QGuiApplication::applicationState() == Qt::ApplicationActive; }, 15000)) QSKIP("Native ratio snapshots need a foreground test application");
        }
#endif
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); }); InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QImage image(size * 1.5, QImage::Format_ARGB32); image.fill(QColor("#e5edf7"));
        { QPainter painter(&image); painter.fillRect(QRect(100, 140, 640, 360), QColor("#8ab89d")); QFont font = painter.font(); font.setPixelSize(28); painter.setFont(font); painter.setPen(QColor("#25354d")); painter.drawText(QPoint(140, 220), "16:9 · Physical pixels"); }
        auto *screen = QGuiApplication::primaryScreen(); QPointer<Overlay> window = new Overlay({image, QRect(screen->availableGeometry().topLeft() + QPoint(30, 50), size), {}, screen}, "annotate", false);
        const auto cleanup = qScopeGuard([&] { delete window.data(); }); window->show(); window->raise(); window->activateWindow(); QTest::qWait(40); QSignalSpy accepted(window, &Overlay::accepted);
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(90, 100)); QTest::mouseMove(window, QPoint(410, 280)); QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(410, 280));
        auto *width = window->findChild<QSpinBox *>("CaptureWidth"); auto *height = window->findChild<QSpinBox *>("CaptureHeight"); auto *ratio = window->findChild<QComboBox *>("CaptureAspectRatio"); auto *tools = window->findChild<QWidget *>("CaptureTools"); auto *actions = window->findChild<QWidget *>("CaptureActions"); QVERIFY(width); QVERIFY(height); QVERIFY(ratio); QVERIFY(tools); QVERIFY(actions);
        QCOMPARE(ratio->itemText(0), language == "en" ? QStringLiteral("Free ratio") : QStringLiteral("自由比例"));
        ratio->setCurrentIndex(3); width->setValue(641); QCOMPARE(QSize(width->value(), height->value()), QSize(640, 360));
        QVERIFY2(comboArrowVisible(ratio), "Ratio dropdown arrows must contain visible foreground pixels");
        const auto buttons = actions->findChildren<QToolButton *>(); QCOMPARE(buttons.size(), 8); for (auto *button : buttons) { QCOMPARE(button->size(), QSize(32, 32)); QCOMPARE(button->iconSize(), QSize(20, 20)); }
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); QTest::qWait(20); QVERIFY(window->grab().save(reviewPath("capture-ratio-selection-" + suffix)));
        ratio->setFocus(); QTRY_VERIFY(ratio->hasFocus()); QVERIFY(tools->grab().save(reviewPath("capture-ratio-focused-" + suffix)));
        QVERIFY2(ratioPopupClosesWithEscape(ratio, reviewPath("capture-ratio-dropdown-" + suffix)), "The real ratio popup must be captured and close after Escape"); QVERIFY(window); QVERIFY(window->isVisible()); QVERIFY(accepted.isEmpty());
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(300, 200)); QTest::keyClick(window, Qt::Key_Return); QTRY_COMPARE(accepted.size(), 1);
        const auto output = qvariant_cast<QImage>(accepted.first()[0]); QCOMPARE(output.size(), QSize(640, 360)); QCOMPARE(output, cropFrame(image, size, qvariant_cast<QRectF>(accepted.first()[1])));
    }
    void actualCaptureCancellationRestoresWindows() {
        QWidget alreadyHidden;
        openPage(0);
        click(item("MainCapture"));
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000); QVERIFY(!window_->isVisible());
        QTest::keyClick(overlay(), Qt::Key_Escape); QTRY_VERIFY(window_->isVisible()); QVERIFY(!alreadyHidden.isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void actualCaptureOpensEditorWithCroppedPixels() {
        controller_->capture();
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000);
        QTest::mousePress(overlay(), Qt::LeftButton, {}, QPoint(50, 50)); QTest::mouseMove(overlay(), QPoint(150, 130)); QTest::mouseRelease(overlay(), Qt::LeftButton, {}, QPoint(150, 130)); QTest::keyClick(overlay(), Qt::Key_Return);
        auto editor = []() -> Editor * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Editor *>(widget)) return result; return nullptr; };
        QTRY_VERIFY(editor()); auto *canvas = editor()->findChild<Canvas *>(); QVERIFY(canvas && !canvas->rendered().isNull());
        QVERIFY(canvas->rendered().width() >= 100); QVERIFY(canvas->rendered().height() >= 80);
        editor()->close(); QVERIFY(window_->isVisible());
    }
    void scrollToolbarStartsForSelectedContent_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("edit") << false; QTest::newRow("cancel") << true;
    }
    void scrollToolbarStartsForSelectedContent() {
        QFETCH(bool, cancel);
        auto *session = controller_->findChild<ScrollSession *>(); QVERIFY(session);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const QRect region(screen.topLeft() + QPoint(40, 100), QSize(qMin(320, screen.width() - 80), qMin(220, screen.height() - 240)));
        const auto cleanup = qScopeGuard([&] {
            { QSignalBlocker blocked(session); session->stop(); }
            session->finished({}, region, {});
        });
        QString failure;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { failure = box->text(); box->accept(); }
        });
        dismiss.start(10); controller_->capture();
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000);
        auto *ordinaryToolbar = overlay()->findChild<QWidget *>("CaptureTools"); QVERIFY(ordinaryToolbar);
        QTest::mousePress(overlay(), Qt::LeftButton, {}, QPoint(40, 80)); QTest::mouseMove(overlay(), QPoint(140, 140));
        QTest::mouseRelease(overlay(), Qt::LeftButton, Qt::ShiftModifier, QPoint(140, 140)); QTRY_VERIFY(ordinaryToolbar->isVisible());
        ordinaryToolbar->adjustSize();
        auto *ordinaryActions = ordinaryToolbar->findChild<QWidget *>("CaptureActions"); QVERIFY(ordinaryActions);
        const auto layoutMargins = ordinaryToolbar->layout()->contentsMargins(); const auto frameMargins = ordinaryToolbar->contentsMargins();
        const int ordinaryHeight = ordinaryActions->height() + layoutMargins.top() + layoutMargins.bottom() + frameMargins.top() + frameMargins.bottom();
        auto *ordinaryEdit = ordinaryToolbar->findChild<QToolButton *>("captureAction-edit"); QVERIFY(ordinaryEdit);
        const QSize ordinaryButton = ordinaryEdit->size(); QCOMPARE(ordinaryButton, QSize(32, 32)); QCOMPARE(ordinaryEdit->iconSize(), QSize(20, 20));
        QImage first(region.size(), QImage::Format_RGB32); first.fill(Qt::white);
        class ShowCounter : public QObject {
        public:
            int shows = 0;
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::Show) ++shows;
                return false;
            }
        } visibility;
        window_->installEventFilter(&visibility);
        overlay()->accepted(first, QRectF(region).translated(-overlay()->geometry().topLeft()), "scroll");
        QTest::qWait(50);
        QVERIFY2(session->active(), qPrintable(failure));
        QCOMPARE(visibility.shows, 0);
        QWidget *hud = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (widget->objectName() == "ScrollHud") hud = widget;
        QVERIFY(hud); QVERIFY(hud->isVisible()); QVERIFY(!hud->frameGeometry().intersects(region));
        QCOMPARE(hud->findChild<QWidget *>("CaptureTools")->height(), ordinaryHeight);
        QCOMPARE(hud->findChild<QToolButton *>("ScrollFinish")->size(), ordinaryButton);
        QCOMPARE(hud->findChild<QToolButton *>("ScrollFinish")->iconSize(), QSize(20, 20));
        if (QGuiApplication::platformName() == "windows") QVERIFY(!hud->isActiveWindow());
        QVERIFY(hud->findChild<QAbstractButton *>("ScrollFinish"));
        auto *selection = hud->findChild<QWidget *>("ScrollSelectionFrame");
        QVERIFY(selection); QVERIFY(selection->isVisible());
        QVERIFY(selection->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(hud->findChild<QLabel *>("ScrollImagePreview"));
        QCOMPARE(hud->findChild<QLabel *>("ScrollDimensions")->text(), QStringLiteral("%1 × %2").arg(first.width()).arg(first.height()));
        QVERIFY(!hud->findChild<QLabel *>("ScrollStatus"));
        auto *finish = hud->findChild<QAbstractButton *>("ScrollFinish");
        QCOMPARE(finish->text(), QStringLiteral("编辑"));
        QTest::mouseClick(cancel ? hud->findChild<QAbstractButton *>("scrollAction-cancel") : finish, Qt::LeftButton);
        QVERIFY(!session->active());
        QCOMPARE(visibility.shows, 1);
        Editor *editor = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Editor *>(widget); candidate && candidate->isVisible()) editor = candidate;
        if (cancel) { QVERIFY(!editor); QVERIFY(window_->isVisible()); return; }
        QVERIFY(editor); QVERIFY(editor->isVisible());
        QCOMPARE(editor->findChild<Canvas *>()->rendered().convertToFormat(QImage::Format_RGB32), first);
        QStringList actions;
        for (auto *action : editor->findChildren<QAction *>()) actions.append(action->text());
        QVERIFY(actions.contains(QStringLiteral("复制图片")));
        QVERIFY(actions.contains(QStringLiteral("保存")));
        QVERIFY(actions.contains(QStringLiteral("贴图")));
        editor->close();
    }
    void rejectedScrollReleasesFirstFrame() {
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->accept(); });
        dismiss.start(10);
        controller_->capture();
        auto overlay = []() -> Overlay * { for (auto *widget : QApplication::topLevelWidgets()) if (auto *result = qobject_cast<Overlay *>(widget)) return result; return nullptr; };
        QTRY_VERIFY_WITH_TIMEOUT(overlay(), 5000);
        overlay()->accepted(trackedImage(), QRectF(QGuiApplication::primaryScreen()->geometry()).translated(-overlay()->geometry().topLeft()), "scroll");
        QTest::qWait(50); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(liveTrackedImages.load(), 0); QVERIFY(window_->isVisible());
    }
    void doubleClickClosePreferencePersistsAndAppliesToNewPins_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme");
        QTest::newRow("zh-light") << "zh_CN" << "light";
        QTest::newRow("en-light") << "en" << "light";
        QTest::newRow("zh-dark") << "zh_CN" << "dark";
        QTest::newRow("en-dark") << "en" << "dark";
    }
    void doubleClickClosePreferencePersistsAndAppliesToNewPins() {
        QFETCH(QString, language); QFETCH(QString, theme);
        const auto original = apply_->settings();
        auto candidate = original;
        auto options = candidate.value("pin").toObject(); options["confirmDoubleClickClose"] = true; candidate["pin"] = options;
        apply_->apply(candidate);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(apply_->settings().value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(true));
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QApplication::setPalette(QGuiApplication::palette());
        QImage source(120, 80, QImage::Format_RGB32); source.fill(Qt::white);
        const auto create = [&]() -> Pin * {
            controller_->findChild<ClipboardController *>()->pinImage(source);
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget)) return pin;
            return nullptr;
        };
        QPointer<Pin> pin = create(); QVERIFY(pin);
        const auto cleanup = qScopeGuard([&] { delete pin.data(); });
        QSignalSpy disabled(pin, &Pin::doubleClickClosePromptDisabled);
        QSignalSpy stored(apply_, &SettingsApply::finished);
        bool prompted = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *box = pin ? pin->findChild<QDialog *>("PinClosePrompt") : nullptr; box && box->isVisible()) {
                prompted = true;
                auto *skip = box->findChild<QCheckBox *>("PinCloseSkip"); QVERIFY(skip); skip->setChecked(true);
                if (QGuiApplication::platformName() == "windows") {
                    QString path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../.impeccable/review/actions");
                    if (qEnvironmentVariable("QT_SCALE_FACTOR") == "1.5") path += "/hidpi";
                    QDir().mkpath(path);
                    QVERIFY(box->grab().save(path + "/pin-close-" + language + "-" + theme + ".png"));
                }
                box->findChild<QPushButton *>("PinCloseConfirm")->click();
            }
        });
        dismiss.start(10);
        QTest::mouseDClick(pin, Qt::LeftButton, {}, QPoint(20, 20));
        QTRY_VERIFY(prompted); QTRY_VERIFY(pin.isNull());
        QCOMPARE(disabled.count(), 1);
        QTRY_VERIFY(!stored.isEmpty() && !apply_->busy());
        QVERIFY2(stored.last()[0].toBool(), qPrintable(stored.last()[3].toString()));
        QTRY_COMPARE(readJson(settingsPath()).value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(false));
        QTRY_COMPARE(apply_->settings().value("pin").toObject().value("confirmDoubleClickClose"), QJsonValue(false));
        prompted = false; pin = create(); QVERIFY(pin);
        QTest::mouseDClick(pin, Qt::LeftButton, {}, QPoint(20, 20));
        QTRY_VERIFY(pin.isNull()); QVERIFY(!prompted);
        apply_->apply(original); QTRY_VERIFY(!apply_->busy());
        InterfacePreferences::instance().apply(original.value("appearance").toObject());
        QApplication::setPalette(QGuiApplication::palette());
    }
    void pinClosePromptKeepsMainWindowBehindWorkspace_data() {
        QTest::addColumn<bool>("mainHidden"); QTest::addColumn<bool>("confirm");
        QTest::newRow("background-cancel") << false << false; QTest::newRow("hidden-cancel") << true << false;
        QTest::newRow("background-close") << false << true; QTest::newRow("hidden-close") << true << true;
    }
    void pinClosePromptKeepsMainWindowBehindWorkspace() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != "windows") QSKIP("Native Windows stacking test");
        QFETCH(bool, mainHidden); QFETCH(bool, confirm);
        QImage image(240, 120, QImage::Format_RGB32); image.fill(QColor("#e2e6ed"));
        QPointer<Pin> pin = new Pin(image, QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(60, 80), image.size()), {}, "en-US");
        const auto cleanup = qScopeGuard([&] { delete pin.data(); window_->show(); });
        pin->show();
        QWidget workspace; workspace.setWindowTitle("Still isolated workspace"); workspace.resize(640, 480); workspace.show(); workspace.raise(); workspace.activateWindow();
        const HWND mainHandle = reinterpret_cast<HWND>(window_->winId());
        const HWND workHandle = reinterpret_cast<HWND>(workspace.winId());
        if (mainHidden) window_->hide();
        else QVERIFY(SetWindowPos(mainHandle, workHandle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
        const auto behind = [&] {
            for (HWND current = GetTopWindow(nullptr); current; current = GetWindow(current, GW_HWNDNEXT)) {
                if (current == workHandle) return true;
                if (current == mainHandle) return false;
            }
            return false;
        };
        QTest::qWait(80); if (!mainHidden) QVERIFY(behind());
        QTest::mouseDClick(pin, Qt::LeftButton, {}, QPoint(20, 20));
        auto *prompt = pin->findChild<QDialog *>("PinClosePrompt"); QVERIFY(prompt && prompt->isVisible());
        QCOMPARE(prompt->windowModality(), Qt::NonModal); QVERIFY(!QApplication::activeModalWidget());
        QTest::qWait(100);
        QVERIFY(!window_->isActiveWindow());
        if (mainHidden) QVERIFY(!window_->isVisible()); else QVERIFY(behind());
        if (confirm) {
            prompt->findChild<QPushButton *>("PinCloseConfirm")->click();
            QTRY_VERIFY(pin.isNull());
        } else {
            QTest::keyClick(prompt, Qt::Key_Escape);
            QTRY_VERIFY(!pin->findChild<QDialog *>("PinClosePrompt")); QVERIFY(pin->isVisible());
        }
        if (mainHidden) QVERIFY(!window_->isVisible()); else QVERIFY(behind());
#else
        QSKIP("Native Windows stacking test");
#endif
    }
    void historyPreferencesMigrateExistingFiles() {
        const QString source = historyDirectory(apply_->settings().value("history").toObject()); QDir().mkpath(source);
        QImage picture(160, 100, QImage::Format_ARGB32); picture.fill(QColor("#dce8fa"));
        QVERIFY(saveImage(picture, QDir(source).filePath("synthetic.png"), nullptr));
        const auto originalFiles = historyFiles(source); QVERIFY(!originalFiles.isEmpty());
        window_->setPage(3); QTRY_VERIFY(!window_->historyItems().isEmpty());
        click(item("Navigation3"));
        for (int i = 0; i < 60 && !item("HistoryList")->hasActiveFocus(); ++i) QTest::keyClick(window_->view(), Qt::Key_Tab);
        QVERIFY(item("HistoryList")->hasActiveFocus());
        QTest::keyClick(window_->view(), Qt::Key_Down);
        QVERIFY(item("HistoryList")->property("currentIndex").toInt() >= 0);
        const QString target = directory_.filePath("other-drive/history");
        click(item("HistoryStorage")); QTRY_VERIFY(item("history_dir"));
        QTest::qWait(80); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("history-storage")));
        click(item("history_dir")); QTest::keyClick(window_->view(), Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(window_->view(), target);
        QTest::keyClick(window_->view(), Qt::Key_Escape);
        QVERIFY(window_->isVisible());
        QTRY_COMPARE(readJson(settingsPath()).value("history").toObject().value("dir").toString(), target);
        window_->setSetting("history", "maxMiB", 64); window_->setSetting("history", "enabled", false);
        QTRY_VERIFY(!apply_->busy());
        const auto options = readJson(settingsPath()).value("history").toObject();
        QCOMPARE(options.value("dir").toString(), target); QCOMPARE(options.value("maxMiB").toInt(), 64); QVERIFY(!options.value("enabled").toBool());
        QVERIFY(historyFiles(source).isEmpty()); QCOMPARE(historyFiles(target).size(), originalFiles.size());
        QTRY_COMPARE(window_->historyItems().size(), originalFiles.size());
    }
    void invalidDirectoryRestoresPreviousValue() {
        const QString original = window_->form().value("history").toMap().value("dir").toString();
        const auto previous = readJson(settingsPath()); QFile occupied(directory_.filePath("occupied-file"));
        QVERIFY(occupied.open(QIODevice::WriteOnly)); occupied.write("keep this file"); occupied.close();
        QSignalSpy finished(apply_, &SettingsApply::finished);
        window_->setSetting("history", "dir", occupied.fileName() + "  ");
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy()); QVERIFY(!finished.last()[0].toBool());
        QCOMPARE(window_->form().value("history").toMap().value("dir").toString(), original);
        QCOMPARE(readJson(settingsPath()), previous); QVERIFY(window_->status().contains(QStringLiteral("未生效")));
        QVERIFY(!QApplication::activeModalWidget());
        click(item("HistoryStorage")); QTRY_VERIFY(item("HistoryStorageStatus"));
        QCOMPARE(item("HistoryStorageStatus")->property("text").toString(), window_->status());
        QTest::qWait(50); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("history-storage-error")));
        QPointer<MainWindow> openWindow = window_;
        QTest::keyClick(window_->view(), Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(openWindow); QVERIFY(openWindow->isVisible());
    }
    void successfulMigrationShowsCleanupWarning_data() {
        QTest::addColumn<int>("mode"); QTest::newRow("visible") << 0; QTest::newRow("hidden") << 1; QTest::newRow("pending-save") << 2;
    }
    void successfulMigrationShowsCleanupWarning() {
#ifdef Q_OS_WIN
        QFETCH(int, mode);
        if (mode != 0 && QSystemTrayIcon::isSystemTrayAvailable()) QSKIP("Notification fallback requires the offscreen platform");
        window_->setPage(3);
        const QString source = historyDirectory(apply_->settings().value("history").toObject());
        const QString path = QDir(source).filePath("locked-migration.png"); QDir().mkpath(source);
        QImage image(20, 20, QImage::Format_ARGB32); image.fill(Qt::white); QVERIFY(image.save(path));
        const auto closeHandle = [](void *handle) { CloseHandle(handle); };
        std::unique_ptr<void, decltype(closeHandle)> handle(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), closeHandle);
        QVERIFY(handle.get() != INVALID_HANDLE_VALUE);
        bool notified = false; QTimer dismissWarning;
        connect(&dismissWarning, &QTimer::timeout, this, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { notified = box->text().contains(QStringLiteral("旧副本未删除")); box->accept(); } });
        dismissWarning.start(10); if (mode == 1) window_->hide();
        QSignalSpy finished(apply_, &SettingsApply::finished);
        window_->setSetting("history", "dir", directory_.filePath(QStringLiteral("warning-history-%1").arg(mode)));
        if (mode == 2) window_->setSetting("history", "enabled", !window_->form().value("history").toMap().value("enabled").toBool());
        QTRY_VERIFY(!finished.isEmpty() && !apply_->busy()); dismissWarning.stop(); window_->show();
        QVERIFY(finished.last()[0].toBool()); const QString warning = finished.first()[3].toString(); QVERIFY(!warning.isEmpty()); QVERIFY(QFile::exists(path));
        if (mode == 0) { QVERIFY(window_->status().contains(warning)); QVERIFY(window_->statusError()); } else QVERIFY(notified);
#else
        QSKIP("Windows file sharing is required");
#endif
    }
    void deletingHistorySurvivesRefreshInsideConfirmation() {
        window_->setPage(3); window_->refreshHistory(); QTRY_VERIFY(!window_->historyItems().isEmpty());
        const QString path = window_->historyItems().first().toMap().value("path").toString();
        bool confirmed = false;
        QTimer::singleShot(0, this, [&] {
            window_->setPage(1); window_->setPage(3);
            if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { confirmed = true; question->button(QMessageBox::Yes)->click(); }
        });
        window_->historyAction(path, "remove"); QVERIFY(confirmed); QTRY_VERIFY(!QFile::exists(path));
    }
    void historyImageOpeningIsAsynchronousAndStopsOnNavigation() {
        auto *history = controller_->findChild<HistoryStore *>();
        QVERIFY(history);
        const auto path = QDir(history->directory()).filePath("open-async.png");
        QImage image(20, 20, QImage::Format_ARGB32); image.fill(Qt::white);
        QVERIFY(saveImage(image, path, nullptr));
        window_->setPage(3);
        window_->refreshHistory();
        QTRY_VERIFY(!window_->historyLoading() && !window_->historyItems().isEmpty());
        QSignalSpy opened(window_, &MainWindow::historyImageRequested);
        window_->historyAction(path, "edit");
        QVERIFY(opened.isEmpty());
        window_->setPage(0);
        QTest::qWait(100);
        QVERIFY(opened.isEmpty());
        window_->setPage(3);
        QTRY_VERIFY(!window_->historyLoading() && !window_->historyItems().isEmpty());
        window_->historyAction(path, "edit");
        QTRY_COMPARE(opened.size(), 1);
        QCOMPARE(qvariant_cast<QImage>(opened.first()[0]).convertToFormat(image.format()), image);
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *editor = qobject_cast<Editor *>(widget)) editor->close();
        history->remove(path);
        QTRY_VERIFY(!QFile::exists(path));
    }
    void snapshotsAndCompactLayout() {
        window_->setPage(1); QTest::qWait(50);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("settings-warning")));
        window_->setSetting("appearance", "theme", "dark"); QTRY_VERIFY(!apply_->busy());
        window_->setSetting("appearance", "theme", "light"); QTRY_VERIFY(!apply_->busy());
        // Synthetic review content stays in this test's isolated clipboard archive.
        auto *archive = controller_->findChild<ClipboardArchive *>(); QVERIFY(archive);
        for (const auto &text : {QStringLiteral("周末计划\n美术馆、河边散步，还有一家一直想去的书店。"),
                                QStringLiteral("Reading list\nA few pages for the train ride home."),
                                QStringLiteral("设计备忘\n让每个元素都有明确的职责，也给内容留一点呼吸空间。")}) {
            QMimeData mime; mime.setText(text); archive->add(captureClipboard(mime));
        }
        QMimeData logo; logo.setImageData(QImage(":/brand/still.png")); archive->add(captureClipboard(logo));
        QMimeData note; note.setText(QStringLiteral("留住眼前，随时找回\n图片、文字与文件，收好每一段值得留下的内容。")); archive->add(captureClipboard(note));
        const QString filePath = directory_.filePath("reading-list.md");
        QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Synthetic file for clipboard review."); file.close();
        QMimeData files; files.setUrls({QUrl::fromLocalFile(filePath)}); archive->add(captureClipboard(files));
        QTRY_COMPARE(window_->clips().size(), 9);
        for (int i = 0; i < window_->clips().size(); ++i) if (window_->clips()[i].toMap().value("preview").toString().startsWith(QStringLiteral("留住眼前"))) window_->setSelectedIndex(i);
        const QString selectedId = window_->selected().value("id").toString();
        window_->resize(1000, 700); window_->setSearch({}); window_->setFilter("all");
        const QStringList names{"clipboard", "general", "hotkeys", "history", "advanced", "clipboard-settings"};
        for (int page = 0; page < names.size(); ++page) {
            window_->setPage(page); QTest::qWait(80); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath(names[page])));
            if (page == 0) {
                auto *action = item("ClipboardMore");
                QVERIFY(action->mapToScene(QPointF()).y() + action->height() <= item("ClipboardList")->mapToScene(QPointF()).y());
                QVERIFY(item("ClipboardList")->mapToScene(QPointF()).y() < 170);
                initialBodyFont_ = item("ClipTitle0")->property("font").value<QFont>();
                QCOMPARE(item("ClipRow0")->height(), 86);
                QVERIFY(item("ClipRow0")->width() > item("ClipboardList")->width() - 20);
                QVERIFY(item("ClipRow5"));
                auto *list = item("ClipboardList"); QVERIFY(list);
                QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
                QTRY_VERIFY(item("ClipRow8"));
                auto *last = item("ClipRow8");
                QTRY_VERIFY(last->mapToScene(QPointF(0, last->height())).y() <= item("MainCapture")->mapToScene(QPointF()).y());
                QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
            }
            if (page == 3 && window_->historyItems().size() == 1) {
                auto *actions = item("HistoryActions"); QVERIFY(actions);
                const QRectF bounds(actions->mapToScene(QPointF()), actions->size());
                QVERIFY(window_->view()->rect().contains(bounds.toAlignedRect()));
            }
        }
        window_->setPage(0); click(item("Filter_files")); QTest::qWait(50);
        QCOMPARE(item("ClipTitle0")->property("text").toString(), QString("reading-list.md"));
        QVERIFY(item("ClipSnippet0")->property("text").toString().contains(QFileInfo(filePath).dir().dirName()));
        QCOMPARE(window_->selected().value("preview").toString(), filePath);
        window_->setPreviewVisible(true); QTRY_VERIFY(window_->previewText().contains("reading-list.md"));
        auto *selectedCard = item("ClipRow0"); auto *grid = item("ClipboardList"); QVERIFY(selectedCard); QVERIFY(grid);
        QTRY_VERIFY(QRectF(grid->mapToScene(QPointF()), grid->size()).contains(QRectF(selectedCard->mapToScene(QPointF()), selectedCard->size())));
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("files")));
        window_->setFilter("all");
        for (int i = 0; i < window_->clips().size(); ++i) if (window_->clips()[i].toMap().value("id").toString() == selectedId) window_->setSelectedIndex(i);
        QTest::qWait(200);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("preview")));
        window_->resize(800, 620); QTest::qWait(50);
        QCOMPARE(window_->size(), QSize(800, 620)); QVERIFY(!item("ClipboardPreview")->isVisible());
        QVERIFY(item("ClipboardPanel")->mapToScene(QPointF()).x() >= 200);
        selectedCard = item("ClipRow" + QString::number(window_->selectedIndex()));
        QTRY_VERIFY(QRectF(grid->mapToScene(QPointF()), grid->size()).contains(QRectF(selectedCard->mapToScene(QPointF()), selectedCard->size())));
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-clipboard")));
        window_->setPage(1); QTest::qWait(30); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-settings")));
        for (const auto &name : {QString("appearance_language"), QString("appearance_theme"), QString("capture_showMagnifier"), QString("capture_afterSelectAction")}) {
            auto *control = item(name); QVERIFY(control);
            scrollSettingIntoView(control);
            const QRectF bounds(control->mapToScene(QPointF()), control->size()); QVERIFY(window_->view()->rect().contains(bounds.toAlignedRect()));
        }
        window_->resize(1000, 700);
    }
    void nativeGlassBackdrop() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Compositor material requires a real Windows desktop");
        const QString originalTheme = window_->form().value("appearance").toMap().value("theme").toString();
        const QRect available = window_->screen()->availableGeometry();
        window_->resize(qMin(1000, available.width() - 32), qMin(700, available.height() - 32));
        window_->move(available.center() - QPoint(window_->width() / 2, window_->height() / 2));
        QLabel backdrop(nullptr, Qt::Tool | Qt::FramelessWindowHint);
        backdrop.setGeometry(window_->geometry().adjusted(-8, -8, 8, 8));
        QPixmap pattern(backdrop.size());
        {
            QPainter painter(&pattern);
            const QStringList colors{"#abc8e4", "#e0b9bf", "#bcbce2", "#a9d0ce"};
            for (int i = 0; i < 4; ++i) painter.fillRect(i * pattern.width() / 4, 0, pattern.width() / 4 + 1, pattern.height(), QColor(colors[i]));
            painter.setPen(QPen(Qt::white, 3));
            for (int y = 20; y < pattern.height(); y += 32) painter.drawLine(0, y, pattern.width(), y);
        }
        backdrop.setPixmap(pattern); backdrop.show();
        window_->show(); window_->raise(); window_->activateWindow();
        for (const auto &theme : {QString("light"), QString("dark")}) {
            window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
            QTest::qWait(250);
            const QImage frame = window_->view()->grabFramebuffer(); QVERIFY(!frame.isNull());
            if (window_->glassAvailable()) QVERIFY(frame.pixelColor(16 * frame.devicePixelRatio(), 100 * frame.devicePixelRatio()).alpha() < 255);
            const auto shot = window_->screen()->grabWindow(0, window_->x(), window_->y(), window_->width(), window_->height());
            QVERIFY(!shot.isNull()); QVERIFY(shot.save(reviewPath("glass-native-" + theme)));
        }
        qInfo() << "Native backdrop enabled:" << window_->glassAvailable();
        window_->setSetting("appearance", "theme", originalTheme); QTRY_VERIFY(!apply_->busy());
    }
    void chineseUiGlyphsUseTheUiFont() {
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires installed Windows UI fonts");
        for (const auto weight : {QFont::Normal, QFont::Medium, QFont::DemiBold}) {
            QFont font = item("LibraryCount")->property("font").value<QFont>();
            font.setPixelSize(15); font.setWeight(weight);
            QTextLayout layout(QStringLiteral("留住眼前，随时找回"), font);
            layout.beginLayout(); layout.createLine(); layout.endLayout();
            const auto runs = layout.glyphRuns(); QVERIFY(!runs.isEmpty());
            for (const auto &run : runs) {
                qInfo() << "Chinese UI glyph family:" << font.family() << weight << run.rawFont().familyName();
                QCOMPARE(run.rawFont().familyName(), QString("Microsoft YaHei UI"));
            }
        }
    }
    void compactEnglishPasteActions() {
        const QString language = window_->form().value("appearance").toMap().value("language").toString();
        const QSize size = window_->size();
        window_->setSetting("appearance", "language", "en"); QTRY_VERIFY(!apply_->busy());
        window_->resize(800, 620); window_->hide(); setPasteTargetForTest(42); controller_->openClipboard();
        QTRY_VERIFY(window_->pasteDefault()); QTest::qWait(80);
        auto bounds = [](QQuickItem *control) { return QRectF(control->mapToScene(QPointF()), control->size()); };
        QVERIFY(bounds(item("Filter_other")).right() <= bounds(item("ClipboardMore")).left());
        QVERIFY(bounds(item("ClipboardMore")).right() <= bounds(item("ClipboardPaste")).left());
        QVERIFY2(QRectF(window_->view()->rect()).contains(bounds(item("ClipboardPaste"))), qPrintable(QDebug::toString(bounds(item("ClipboardPaste")))));
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-en-paste")));
        window_->setSetting("appearance", "language", language); QTRY_VERIFY(!apply_->busy());
        window_->resize(size);
    }
    void bilingualThemesAndControls() {
        window_->setPage(1); QTest::qWait(30);
        auto choose = [&](const QString &name, int steps) {
            QTest::qWait(40);
            if (name == "appearance_language") {
                click(item(steps > 0 ? "Language_en" : "Language_zh_CN"));
                return;
            }
            if (name == "appearance_theme") {
                const QStringList values{"system", "light", "dark"};
                const int current = values.indexOf(window_->form().value("appearance").toMap().value("theme").toString());
                click(item("Theme_" + values[qBound(0, current + steps, 2)]));
                return;
            }
            click(item(name));
            for (int i = 0; i < qAbs(steps); ++i) QTest::keyClick(window_->view(), steps > 0 ? Qt::Key_Down : Qt::Key_Up);
            QTest::keyClick(window_->view(), Qt::Key_Return);
        };
        choose("capture_afterSelectAction", 1);
        QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("capture").toObject().value("afterSelectAction").toString(), QString("copy"));
        choose("capture_afterSelectAction", -1); QTRY_VERIFY(!apply_->busy());
        const auto clips = window_->clips();
        choose("appearance_language", 1);
        QTRY_COMPARE(item("PageTitle")->property("text").toString(), QString("General"));
        QCOMPARE(window_->windowTitle(), QString("Still"));
        QCOMPARE(item("ClipTitle0")->property("font").value<QFont>(), initialBodyFont_);
        QCOMPARE(item("capture_afterSelectAction")->property("currentText").toString(), QString("Open editor"));
        QCOMPARE(readJson(settingsPath()).value("appearance").toObject().value("language").toString(), QString("en"));
        QImage picture(400, 240, QImage::Format_RGB32); picture.fill(Qt::white);
        const QString path = directory_.filePath("language-check.png"); QVERIFY(picture.save(path));
        controller_->openImage(path); Editor *editor = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Editor *>(widget)) editor = candidate;
        QVERIFY(editor); QCOMPARE(editor->windowTitle(), QString("Still · Edit capture"));
        window_->activateWindow(); window_->view()->setFocus();
        choose("appearance_theme", 1);
        QTRY_VERIFY(!apply_->busy());
        QTRY_VERIFY(window_->dark()); QVERIFY(QGuiApplication::palette().color(QPalette::Window).lightness() < 60);
        QCOMPARE(editor->palette().color(QPalette::Window), QGuiApplication::palette().color(QPalette::Window));
        QCOMPARE(readJson(settingsPath()).value("appearance").toObject().value("theme").toString(), QString("dark"));
        for (int page = 0; page <= 5; ++page) {
            window_->setPage(page); QTest::qWait(80);
            if (page == 0) {
                auto *grid = item("ClipboardList"); auto *selectedCard = item("ClipRow" + QString::number(window_->selectedIndex()));
                QVERIFY(grid); QVERIFY(selectedCard);
                QTRY_VERIFY(QRectF(grid->mapToScene(QPointF()), grid->size()).contains(QRectF(selectedCard->mapToScene(QPointF()), selectedCard->size())));
            }
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-en-" + QString::number(page))));
        }
        window_->resize(800, 620);
        for (int page : {0, 1, 5}) {
            window_->setPage(page); QTest::qWait(50);
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("compact-en-" + QString::number(page))));
        }
        window_->resize(1000, 700); window_->setPage(1);
        choose("appearance_theme", -1); QTRY_VERIFY(!window_->dark());
        QTest::qWait(50); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("light-en-general")));
        choose("appearance_language", -1);
        QTRY_COMPARE(item("PageTitle")->property("text").toString(), QStringLiteral("通用偏好"));
        QCOMPARE(editor->windowTitle(), QStringLiteral("拾影 · 编辑截图")); editor->close();
        choose("appearance_theme", -1); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(window_->dark(), QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        window_->setPage(5);
        auto *number = item("clipboard_maxItems"); QVERIFY(number);
        const int before = number->property("value").toInt(); number->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Up); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("clipboard").toObject().value("maxItems").toInt(), before + 1);
        QTest::keyClick(window_->view(), Qt::Key_Down); QTRY_VERIFY(!apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("clipboard").toObject().value("maxItems").toInt(), before);
        QCOMPARE(window_->clips().size(), clips.size());
        for (int i = 0; i < clips.size(); ++i) if (clips[i].toMap().value("kind") == "text") QCOMPARE(window_->clips()[i].toMap().value("title"), clips[i].toMap().value("title"));
        window_->setPage(1);
    }
    void darkInteractionStates() {
        const QString previous = window_->form().value("appearance").toMap().value("theme").toString();
        window_->setSetting("appearance", "theme", "dark"); QTRY_VERIFY(!apply_->busy()); QTRY_VERIFY(window_->dark());
        window_->setPage(0); window_->togglePause(); QTRY_VERIFY(window_->paused()); QTest::qWait(50);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-paused")));
        if (QGuiApplication::platformName() == "windows") {
            auto *row = item("ClipRow" + QString::number(window_->selectedIndex())); QVERIFY(row);
            movePointerTo(row);
            auto *remove = item("Delete" + QString::number(window_->selectedIndex())); QVERIFY(remove);
            QTRY_VERIFY(remove->isVisible());
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-row-actions")));
        }
        auto *capture = item("MainCapture"); QVERIFY(capture);
        const QPoint point = capture->mapToScene(QPointF(capture->width() / 2, capture->height() / 2)).toPoint();
        QTest::mousePress(window_->view(), Qt::LeftButton, {}, point); QVERIFY(capture->property("down").toBool()); QTest::qWait(50);
        QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-pressed")));
        // Release outside the capture button so this state check does not start a capture.
        QTest::mouseMove(window_->view(), QPoint(300, 30));
        QTest::mouseRelease(window_->view(), Qt::LeftButton, {}, QPoint(300, 30));
        QVERIFY(!capture->property("down").toBool());
        QVERIFY(window_->isVisible());
        window_->togglePause(); QVERIFY(!window_->paused());
        window_->setPage(1); QTest::qWait(30);
        auto *toggle = item("capture_showMagnifier"); QVERIFY(toggle); toggle->forceActiveFocus();
        QVERIFY(toggle->hasActiveFocus());
        auto *indicator = toggle->property("indicator").value<QQuickItem *>(); QVERIFY(indicator);
        QCOMPARE(indicator->property("border").value<QObject *>()->property("color").value<QColor>(), QColor("#a8a8a8"));
        QTest::qWait(50); QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("dark-switch-focus")));
        window_->setSetting("appearance", "theme", previous); QTRY_VERIFY(!apply_->busy()); window_->setPage(1);
    }
    void contextMenuFollowsAppearanceAndInvokesAction() {
        window_->activateWindow(); window_->view()->setFocus();
        const QString previous = window_->form().value("appearance").toMap().value("theme").toString();
        for (const QString &theme : {QString("light"), QString("dark")}) {
            window_->setSetting("appearance", "theme", theme); QTRY_VERIFY(!apply_->busy());
            window_->setPage(0); click(item("ClipboardMore"));
            auto *menu = window_->view()->rootObject()->findChild<QObject *>("ClipboardContextMenu"); QVERIFY(menu);
            QTRY_VERIFY(menu->property("opened").toBool());
            QPointer<MainWindow> openWindow = window_;
            QTest::keyClick(window_->view(), Qt::Key_Escape);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(openWindow); QVERIFY(openWindow->isVisible()); QTRY_VERIFY(!menu->property("visible").toBool());
            click(item("ClipboardMore")); QTRY_VERIFY(menu->property("visible").toBool());
            QTRY_VERIFY(item("ClipboardMenuCopy"));
            QCOMPARE(item("ClipboardMenuSurface")->property("color").value<QColor>(), QColor(theme == "dark" ? "#1c1c1c" : "#ffffff"));
            auto *label = item("ClipboardMenuCopy")->property("contentItem").value<QQuickItem *>(); QVERIFY(label);
            QCOMPARE(label->property("color").value<QColor>(), QColor(theme == "dark" ? "#ededed" : "#242731"));
            QTest::keyClick(window_->view(), Qt::Key_Down); QTest::qWait(50);
            QVERIFY(window_->view()->grabFramebuffer().save(reviewPath("menu-" + theme)));
            QTest::keyClick(window_->view(), Qt::Key_Escape); QTRY_VERIFY(!menu->property("visible").toBool());
            QVERIFY(window_->isVisible());
            const bool pinned = window_->selected().value("pinned").toBool();
            click(item("ClipboardMore")); QTRY_VERIFY(menu->property("visible").toBool());
            click(item("ClipboardMenuPin")); QTRY_COMPARE(window_->selected().value("pinned").toBool(), !pinned);
            QTRY_VERIFY(!menu->property("visible").toBool()); window_->togglePin();
        }
        window_->setSetting("appearance", "theme", previous); QTRY_VERIFY(!apply_->busy()); window_->setPage(1);
    }
    void settingsCommitSurvivesClosingWindow() {
        window_->setPage(1);
        QSignalSpy finished(apply_, &SettingsApply::finished);
        QPointer<MainWindow> previous = window_;
        window_->setDraft("ocr", "language", "zh-Hans-CN"); window_->setSetting("clipboard", "pasteOnSelect", false);
        item("capture_showMagnifier")->forceActiveFocus();
        QTest::keyClick(window_->view(), Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(previous.isNull());
        window_ = nullptr; QTRY_VERIFY(!finished.isEmpty() && !apply_->busy());
        QCOMPARE(readJson(settingsPath()).value("ocr").toObject().value("language").toString(), QString("zh-Hans-CN"));
        QVERIFY(!readJson(settingsPath()).value("clipboard").toObject().value("pasteOnSelect").toBool());
        controller_->openSettings(); locateWindow(); QVERIFY(window_); QCOMPARE(window_->view()->status(), QQuickWidget::Ready);
        QCOMPARE(window_->page(), 1);
    }
    void annotationObjectEditsWorkNatively() {
        QImage image(900, 500, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        const Annotation rectangle{Tool::Rectangle, {QPointF(100, 100), QPointF(300, 250)}, QColor("#e04452"), 3, {}};
        canvas->document.add(rectangle);
        canvas->document.add({Tool::Text, {QPointF(450, 100)}, QColor("#2463eb"), 4, QStringLiteral("Edit me")});
        canvas->document.add({Tool::Number, {QPointF(600, 300)}, QColor("#3a8e62"), 3, QStringLiteral("7")});
        canvas->undo(); canvas->redo(); editor->show(); QTest::qWait(40);
        auto action = [&](const QString &name) -> QAction * {
            for (auto *candidate : editor->findChildren<QAction *>()) if (candidate->text() == name) return candidate;
            return nullptr;
        };
        auto *select = editor->findChild<QAction *>("selectAnnotation"); QVERIFY(select); QCOMPARE(select->shortcut(), QKeySequence("V"));
        auto *editText = editor->findChild<QAction *>("annotationTextEdit"); QVERIFY(editText);
        auto *width = editor->findChild<QSpinBox *>("annotationWidth"); QVERIFY(width);
        auto *widthText = width->findChild<QLineEdit *>(); QVERIFY(widthText);
        QVERIFY(widthText->width() >= widthText->fontMetrics().horizontalAdvance(width->text()));
        auto *color = editor->findChild<QAction *>("annotationColor"); QVERIFY(color);
        auto *undo = action(t("撤销")); auto *redo = action(t("重做")); QVERIFY(undo); QVERIFY(redo);
        auto point = [&](QPointF source) {
            const QRectF view = canvas->imageRect();
            return (view.topLeft() + QPointF(source.x() * view.width() / image.width(), source.y() * view.height() / image.height())).toPoint();
        };
        const auto unselected = canvas->rendered(); select->trigger(); QVERIFY(canvas->selectionMode());
        const QPoint start = point(QPointF(100, 160)); const QPoint end = point(QPointF(120, 172));
        QTest::mouseClick(canvas, Qt::LeftButton, {}, start); QCOMPARE(canvas->selectedAnnotation(), 0);
        QCOMPARE(canvas->rendered(), unselected); QCOMPARE(width->value(), 3); QVERIFY(!editText->isEnabled());
        QTest::mousePress(canvas, Qt::LeftButton, {}, start); QTest::mouseMove(canvas, end); QTest::mouseRelease(canvas, Qt::LeftButton, {}, end);
        const auto moved = canvas->document.annotations()[0];
        const QRectF view = canvas->imageRect();
        const QPointF delta((end.x() - start.x()) * image.width() / view.width(), (end.y() - start.y()) * image.height() / view.height());
        QCOMPARE(moved.points.size(), 2);
        for (int i = 0; i < moved.points.size(); ++i) QVERIFY(QLineF(moved.points[i], rectangle.points[i] + delta).length() < 0.01);
        undo->trigger(); QVERIFY(canvas->document.annotations()[0] == rectangle); QCOMPARE(canvas->selectedAnnotation(), -1);
        redo->trigger(); QVERIFY(canvas->document.annotations()[0] == moved); QCOMPARE(canvas->selectedAnnotation(), -1);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, end); QCOMPARE(canvas->selectedAnnotation(), 0);
        canvas->setColor(QColor("#2463eb")); canvas->setLineWidth(7);
        QCOMPARE(canvas->document.annotations()[0].color, QColor("#2463eb")); QCOMPARE(canvas->document.annotations()[0].width, qreal(7)); QCOMPARE(width->value(), 7);
        undo->trigger(); QCOMPARE(canvas->document.annotations()[0].width, qreal(3));
        undo->trigger(); QCOMPARE(canvas->document.annotations()[0].color, rectangle.color);
        undo->trigger(); QVERIFY(canvas->document.annotations()[0] == rectangle);
        redo->trigger(); redo->trigger(); redo->trigger();
        const auto edited = canvas->document.annotations()[0]; QCOMPARE(edited.color, QColor("#2463eb")); QCOMPARE(edited.width, qreal(7));
        QTest::mouseClick(canvas, Qt::LeftButton, {}, end); QCOMPARE(canvas->selectedAnnotation(), 0);
        canvas->removeLast(); QCOMPARE(canvas->document.annotations().size(), 2); QCOMPARE(canvas->document.annotations()[0].tool, Tool::Text);
        undo->trigger(); QCOMPARE(canvas->document.annotations().size(), 3); QVERIFY(canvas->document.annotations()[0] == edited); QCOMPARE(canvas->selectedAnnotation(), -1);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, point(QPointF(465, 110))); QCOMPARE(canvas->selectedAnnotation(), 1); QVERIFY(editText->isEnabled());
        const auto selected = canvas->rendered(); canvas->setSelectionMode(false); QCOMPARE(canvas->rendered(), selected);
        QCOMPARE(canvas->document.original(), image);
    }
    void annotationTextEditKeepsTheEditorUsable() {
        QImage image(800, 450, QImage::Format_ARGB32); image.fill(Qt::white);
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Text, {QPointF(100, 100)}, QColor("#3975dc"), 4, QStringLiteral("Initial note")});
        canvas->undo(); canvas->redo(); editor->show(); editor->raise(); editor->activateWindow();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitForWindowActive(editor, 15000))
            QSKIP("Native keyboard/focus checks need an unlocked desktop and foreground test app");
#endif
        QTRY_VERIFY(editor->isActiveWindow());
        auto *select = editor->findChild<QAction *>("selectAnnotation"); auto *editText = editor->findChild<QAction *>("annotationTextEdit");
        QVERIFY(select); QVERIFY(editText); QCOMPARE(editText->shortcut(), QKeySequence(Qt::Key_F2)); select->trigger();
        auto point = [&](QPointF source) {
            const QRectF view = canvas->imageRect();
            return (view.topLeft() + QPointF(source.x() * view.width() / image.width(), source.y() * view.height() / image.height())).toPoint();
        };
        QTest::mouseClick(canvas, Qt::LeftButton, {}, point(QPointF(115, 110))); QCOMPARE(canvas->selectedAnnotation(), 0); QVERIFY(editText->isEnabled());
        bool handled = false; QString initial; QTimer accept;
        connect(&accept, &QTimer::timeout, this, [&] {
            auto *dialog = canvas->findChild<QInputDialog *>(); if (!dialog || !dialog->isVisible()) return;
            accept.stop(); handled = true; initial = dialog->textValue(); dialog->setTextValue(QStringLiteral("Changed note")); dialog->accept();
        });
        accept.start(10); QTest::keyClick(canvas, Qt::Key_F2); accept.stop();
        QVERIFY(handled); QCOMPARE(initial, QStringLiteral("Initial note")); QCOMPARE(canvas->document.annotations()[0].text, QStringLiteral("Changed note"));
        QVERIFY(editor->isVisible());
        if (QGuiApplication::platformName() != "offscreen") QTRY_VERIFY(editor->isActiveWindow());
        canvas->undo(); QCOMPARE(canvas->document.annotations()[0].text, QStringLiteral("Initial note"));
        canvas->redo(); QCOMPARE(canvas->document.annotations()[0].text, QStringLiteral("Changed note"));
        if (QGuiApplication::platformName() == "offscreen") { editor->activateWindow(); QTRY_VERIFY(editor->isActiveWindow()); }
        select->trigger(); QTest::mouseClick(canvas, Qt::LeftButton, {}, point(QPointF(115, 110))); QCOMPARE(canvas->selectedAnnotation(), 0);
        handled = false; QTimer cancel;
        connect(&cancel, &QTimer::timeout, this, [&] {
            auto *dialog = canvas->findChild<QInputDialog *>(); if (!dialog || !dialog->isVisible()) return;
            cancel.stop(); handled = true; initial = dialog->textValue(); dialog->setTextValue(QStringLiteral("Discard this change")); dialog->reject();
        });
        cancel.start(10); QTest::keyClick(canvas, Qt::Key_F2); cancel.stop();
        QVERIFY(handled); QCOMPARE(initial, QStringLiteral("Changed note")); QCOMPARE(canvas->document.annotations()[0].text, QStringLiteral("Changed note"));
        QVERIFY(editor->isVisible());
        if (QGuiApplication::platformName() != "offscreen") QTRY_VERIFY(editor->isActiveWindow());
        auto *rectangle = editor->findChild<QAction *>("annotationTool-0"); QVERIFY(rectangle); rectangle->trigger();
        QTest::mousePress(canvas, Qt::LeftButton, {}, point(QPointF(150, 250))); QTest::mouseMove(canvas, point(QPointF(300, 330))); QTest::mouseRelease(canvas, Qt::LeftButton, {}, point(QPointF(300, 330)));
        QCOMPARE(canvas->document.annotations().size(), 2); QCOMPARE(canvas->document.annotations()[1].tool, Tool::Rectangle);
        canvas->undo(); QCOMPARE(canvas->document.annotations().size(), 1); QCOMPARE(canvas->document.annotations()[0].text, QStringLiteral("Changed note"));
        canvas->redo(); QCOMPARE(canvas->document.annotations().size(), 2); QCOMPARE(canvas->document.original(), image);
    }
    void annotationObjectEditorSnapshots_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme"); QTest::addColumn<QSize>("size");
        QTest::newRow("normal-light-zh") << QString("zh_CN") << QString("light") << QSize(1100, 760);
        QTest::newRow("normal-dark-en") << QString("en") << QString("dark") << QSize(1100, 760);
        QTest::newRow("compact-light-en") << QString("en") << QString("light") << QSize(800, 620);
        QTest::newRow("compact-dark-zh") << QString("zh_CN") << QString("dark") << QSize(800, 620);
    }
    void annotationObjectEditorSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QImage image(900, 500, QImage::Format_ARGB32); image.fill(Qt::white);
        { QPainter painter(&image); painter.setPen(QColor("#64748b")); painter.drawText(QRect(675, 250, 140, 100), Qt::TextWordWrap, "Synthetic sample\nLocal document\nReference 2406"); }
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(80, 65), QPointF(320, 245)}, QColor("#df475a"), 4, {}});
        canvas->document.add({Tool::Arrow, {QPointF(390, 180), QPointF(640, 100)}, QColor("#3975dc"), 5, {}});
        canvas->document.add({Tool::Highlight, {QPointF(70, 335), QPointF(300, 335)}, QColor("#f4bc45"), 5, {}});
        canvas->document.add({Tool::Mosaic, {QPointF(660, 235), QPointF(820, 365)}, QColor("#000000"), 5, {}});
        canvas->document.add({Tool::Text, {QPointF(380, 290)}, QColor("#3975dc"), 4, QStringLiteral("项目说明 / Project note")});
        canvas->document.add({Tool::Number, {QPointF(600, 400)}, QColor("#3a8e62"), 4, QStringLiteral("1")});
        canvas->undo(); canvas->redo(); editor->resize(size); editor->show(); editor->activateWindow(); QTest::qWait(40);
        auto *select = editor->findChild<QAction *>("selectAnnotation"); QVERIFY(select); select->trigger();
        const QRectF view = canvas->imageRect();
        const QPoint textPoint = (view.topLeft() + QPointF(395 * view.width() / image.width(), 300 * view.height() / image.height())).toPoint();
        QTest::mouseClick(canvas, Qt::LeftButton, {}, textPoint); QCOMPARE(canvas->selectedAnnotation(), 4);
        auto *editText = editor->findChild<QAction *>("annotationTextEdit"); QVERIFY(editText); QVERIFY(editText->isEnabled());
        auto *width = editor->findChild<QSpinBox *>("annotationWidth"); QVERIFY(width); QCOMPARE(width->value(), 4);
        auto *tools = editor->findChild<QToolBar *>("CaptureTools"); QVERIFY(tools);
        int drawingTools = 0; for (auto *entry : tools->actions()) if (entry->objectName().startsWith("annotationTool-")) ++drawingTools; QCOMPARE(drawingTools, 11);
        QTest::qWait(20); const QString suffix = QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(editor->grab().save(reviewPath("annotation-edit-" + suffix))); QVERIFY(tools->grab().save(reviewPath("annotation-tools-" + suffix)));
    }
    void imageTransformsKeepNativeKeyboardUsable() {
        QImage image(480, 320, QImage::Format_ARGB32); image.fill(QColor("#edf3fa"));
        { QPainter painter(&image); painter.fillRect(QRect(40, 50, 180, 160), QColor("#94b6d9")); painter.fillRect(QRect(270, 90, 130, 180), QColor("#b4cfbc")); }
        QPointer<Editor> editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor.data(); });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(60, 70), QPointF(200, 190)}, QColor("#df475a"), 4, {}});
        canvas->undo(); canvas->redo(); editor->show(); editor->raise(); editor->activateWindow();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitForWindowActive(editor, 15000))
            QSKIP("Native keyboard/focus checks need an unlocked desktop and foreground test app");
#endif
        QTRY_COMPARE(QApplication::activeWindow(), editor.data()); canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        auto point = [&](QPointF current) {
            const QRectF view = canvas->imageRect(); const auto size = canvas->document.size();
            return (view.topLeft() + QPointF(current.x() * view.width() / size.width(), current.y() * view.height() / size.height())).toPoint();
        };
        auto drag = [&](QPointF start, QPointF end) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, point(start)); QTest::mouseMove(canvas, point(end)); QTest::mouseRelease(canvas, Qt::LeftButton, {}, point(end));
        };
        auto shortcut = [&](QKeySequence::StandardKey key) {
            const auto combination = QKeySequence::keyBindings(key).first()[0];
            QTest::keyClick(canvas, combination.key(), combination.keyboardModifiers());
        };
        const auto before = canvas->rendered(); QTest::keyClick(canvas, Qt::Key_C); QVERIFY(canvas->cropMode());
        drag(QPointF(40, 30), QPointF(440, 290)); const auto firstArea = canvas->cropSelection(); QVERIFY(!firstArea.isEmpty());
        const auto first = before.copy(firstArea); QTest::keyClick(canvas, Qt::Key_Return); QVERIFY(!canvas->cropMode()); QCOMPARE(canvas->rendered(), first);
        shortcut(QKeySequence::Undo); QCOMPARE(canvas->rendered(), before); shortcut(QKeySequence::Redo); QCOMPARE(canvas->rendered(), first);
        auto *horizontal = editor->findChild<QAction *>("flipHorizontal"); QVERIFY(horizontal); horizontal->trigger();
        const auto flipped = first.mirrored(true, false); QCOMPARE(canvas->rendered(), flipped); QTRY_VERIFY(editor->isActiveWindow());
        QTest::keyClick(canvas, Qt::Key_C); QVERIFY(canvas->cropMode());
        drag(QPointF(20, 20), QPointF(flipped.width() - 20, flipped.height() - 20)); const auto secondArea = canvas->cropSelection(); QVERIFY(!secondArea.isEmpty());
        const auto second = flipped.copy(secondArea); QTest::keyClick(canvas, Qt::Key_C); QVERIFY(!canvas->cropMode()); QCOMPARE(canvas->rendered(), second);
        shortcut(QKeySequence::Undo); QCOMPARE(canvas->rendered(), flipped); shortcut(QKeySequence::Redo); QCOMPARE(canvas->rendered(), second);
        QTest::keyClick(canvas, Qt::Key_C); QVERIFY(canvas->cropMode());
        drag(QPointF(10, 10), QPointF(second.width() - 10, second.height() - 10)); QVERIFY(!canvas->cropSelection().isEmpty());
        QTest::keyClick(canvas, Qt::Key_Escape); QVERIFY(!canvas->cropMode()); QVERIFY(editor->isVisible()); QCOMPARE(canvas->rendered(), second);
        shortcut(QKeySequence::Undo); QCOMPARE(canvas->rendered(), flipped); shortcut(QKeySequence::Redo); QCOMPARE(canvas->rendered(), second);
        QCOMPARE(canvas->document.original(), image); QCOMPARE(canvas->document.annotations().size(), 1);
        QTest::keyClick(canvas, Qt::Key_Escape); QTRY_VERIFY(editor.isNull());
    }
    void cropAndFlipRetainEditableAnnotations() {
        QImage image(480, 320, QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) image.setPixelColor(x, y, QColor((x * 3 + y) % 256, (x + y * 5) % 256, (x * 7 + y * 3) % 256));
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(60, 50), QPointF(180, 150)}, QColor("#df475a"), 4, {}});
        canvas->document.add({Tool::Text, {QPointF(210, 70)}, QColor("#3975dc"), 3, QStringLiteral("Editable\ntext")});
        canvas->document.add({Tool::Mosaic, {QPointF(260, 180), QPointF(360, 250)}, QColor("#000000"), 4, {}});
        canvas->undo(); canvas->redo(); editor->show(); QTest::qWait(40);
        auto point = [&](QPointF current) {
            const QRectF view = canvas->imageRect(); const auto size = canvas->document.size();
            return (view.topLeft() + QPointF(current.x() * view.width() / size.width(), current.y() * view.height() / size.height())).toPoint();
        };
        auto *crop = editor->findChild<QAction *>("cropImage"); QVERIFY(crop); QCOMPARE(crop->shortcut(), QKeySequence("C")); QCOMPARE(crop->text(), t("裁切图片"));
        auto *horizontal = editor->findChild<QAction *>("flipHorizontal"); auto *vertical = editor->findChild<QAction *>("flipVertical"); QVERIFY(horizontal); QVERIFY(vertical);
        const auto before = canvas->rendered(); crop->trigger(); QVERIFY(canvas->cropMode());
        QTest::mousePress(canvas, Qt::LeftButton, {}, point(QPointF(30, 25))); QTest::mouseMove(canvas, point(QPointF(430, 285))); QTest::mouseRelease(canvas, Qt::LeftButton, {}, point(QPointF(430, 285)));
        const QRect selection = canvas->cropSelection(); QVERIFY(!selection.isEmpty()); QVERIFY(image.rect().contains(selection)); QCOMPARE(canvas->rendered(), before); QCOMPARE(crop->text(), t("应用裁切"));
        const auto cropped = before.copy(selection); QVERIFY(canvas->applyCrop()); QCOMPARE(canvas->rendered(), cropped); QCOMPARE(canvas->document.size(), selection.size()); QCOMPARE(canvas->document.annotations().size(), 3); QCOMPARE(crop->text(), t("裁切图片"));
        canvas->undo(); QCOMPARE(canvas->rendered(), before); canvas->redo(); QCOMPARE(canvas->rendered(), cropped);
        horizontal->trigger(); QCOMPARE(canvas->rendered(), cropped.mirrored(true, false));
        vertical->trigger(); const auto flipped = cropped.mirrored(true, true); QCOMPARE(canvas->rendered(), flipped);
        auto *select = editor->findChild<QAction *>("selectAnnotation"); QVERIFY(select); select->trigger();
        const auto bounds = canvas->document.bounds(0); const QPointF edge(bounds.left(), bounds.center().y());
        QCOMPARE(canvas->document.hitTest(edge, 2), 0);
        const QPoint start = point(edge); const QPoint end = start + QPoint(12, 8);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, start); QCOMPARE(canvas->selectedAnnotation(), 0);
        const auto annotation = canvas->document.annotations()[0];
        QTest::mousePress(canvas, Qt::LeftButton, {}, start); QTest::mouseMove(canvas, end); QTest::mouseRelease(canvas, Qt::LeftButton, {}, end);
        QVERIFY(!(canvas->document.annotations()[0] == annotation)); const auto edited = canvas->rendered(); QVERIFY(edited != flipped);
        canvas->undo(); QCOMPARE(canvas->rendered(), flipped); canvas->redo(); QCOMPARE(canvas->rendered(), edited);
        canvas->undo(); canvas->undo(); QCOMPARE(canvas->rendered(), cropped.mirrored(true, false));
        canvas->undo(); QCOMPARE(canvas->rendered(), cropped); canvas->undo(); QCOMPARE(canvas->rendered(), before);
        QCOMPARE(canvas->document.original(), image); QCOMPARE(canvas->document.annotations().size(), 3);
    }
    void imageTransformSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void imageTransformSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QImage image(900, 500, QImage::Format_ARGB32); image.fill(Qt::white);
        {
            QPainter painter(&image); painter.fillRect(QRect(0, 0, 900, 72), QColor("#edf3fa"));
            QFont heading = painter.font(); heading.setPixelSize(24); painter.setFont(heading); painter.setPen(QColor("#25354d")); painter.drawText(QPoint(40, 45), "Project board · 工作记录");
            QFont body = painter.font(); body.setPixelSize(16); painter.setFont(body);
            painter.fillRect(QRect(70, 110, 230, 150), QColor("#edf4f0")); painter.drawText(QRect(90, 135, 190, 100), Qt::TextWordWrap, "01 / Capture\nLocal reference\nKeep annotations editable");
            painter.fillRect(QRect(660, 140, 170, 120), QColor("#eef1f8")); painter.drawText(QRect(675, 155, 140, 100), Qt::TextWordWrap, "Synthetic sample\nReference 2406\nLocal document");
            painter.setPen(QColor("#8191a4")); painter.drawText(QPoint(75, 365), "Review the selected area");
        }
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(50, 90), QPointF(320, 290)}, QColor("#df475a"), 4, {}});
        canvas->document.add({Tool::Arrow, {QPointF(380, 200), QPointF(620, 125)}, QColor("#3975dc"), 5, {}});
        canvas->document.add({Tool::Highlight, {QPointF(70, 360), QPointF(300, 360)}, QColor("#f4bc45"), 5, {}});
        canvas->document.add({Tool::Mosaic, {QPointF(660, 145), QPointF(820, 250)}, QColor("#000000"), 5, {}});
        canvas->document.add({Tool::Text, {QPointF(370, 330)}, QColor("#3975dc"), 4, QStringLiteral("裁剪后仍可编辑 / Editable")});
        canvas->document.add({Tool::Number, {QPointF(680, 375)}, QColor("#3a8e62"), 4, QStringLiteral("1")});
        canvas->undo(); canvas->redo(); editor->resize(size); editor->show(); QTest::qWait(40);
        auto *crop = editor->findChild<QAction *>("cropImage"); QVERIFY(crop); QCOMPARE(crop->shortcut(), QKeySequence("C")); QCOMPARE(crop->text(), t("裁切图片")); crop->trigger(); QVERIFY(canvas->cropMode());
        const QRectF view = canvas->imageRect();
        auto point = [&](QPointF source) { return (view.topLeft() + QPointF(source.x() * view.width() / image.width(), source.y() * view.height() / image.height())).toPoint(); };
        QTest::mousePress(canvas, Qt::LeftButton, {}, point(QPointF(35, 55))); QTest::mouseMove(canvas, point(QPointF(850, 450))); QTest::mouseRelease(canvas, Qt::LeftButton, {}, point(QPointF(850, 450)));
        QVERIFY(!canvas->cropSelection().isEmpty()); QCOMPARE(canvas->document.size(), image.size()); QCOMPARE(crop->text(), t("应用裁切"));
        auto *tools = editor->findChild<QToolBar *>("CaptureTools"); QVERIFY(tools);
        int drawingTools = 0; for (auto *entry : tools->actions()) if (entry->objectName().startsWith("annotationTool-")) ++drawingTools; QCOMPARE(drawingTools, 11);
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); QTest::qWait(20);
        QVERIFY(editor->grab().save(reviewPath("image-crop-" + suffix))); QVERIFY(tools->grab().save(reviewPath("image-transform-tools-" + suffix)));
        auto *menu = editor->findChild<QMenu *>("ImageTransformMenu"); QVERIFY(menu); QCOMPARE(menu->actions().size(), 6);
        QCOMPARE(menu->actions()[0]->objectName(), QString("flipHorizontal")); QCOMPARE(menu->actions()[1]->objectName(), QString("flipVertical"));
        QCOMPARE(menu->actions()[0]->text(), t("水平翻转")); QCOMPARE(menu->actions()[1]->text(), t("垂直翻转"));
        QVERIFY(menu->actions()[2]->isSeparator()); QCOMPARE(menu->actions()[3]->objectName(), QString("grayscaleImage")); QCOMPARE(menu->actions()[4]->objectName(), QString("invertImage"));
        QCOMPARE(menu->actions()[3]->text(), t("灰度")); QCOMPARE(menu->actions()[4]->text(), t("反色"));
        QCOMPARE(menu->actions()[5]->objectName(), QString("brightnessImage")); QCOMPARE(menu->actions()[5]->text(), t("调整亮度…")); QVERIFY(menu->actions()[5]->shortcut().isEmpty());
        const auto closeMenu = qScopeGuard([&] { menu->close(); });
        QTimer dismiss; dismiss.setSingleShot(true); connect(&dismiss, &QTimer::timeout, menu, &QMenu::close); dismiss.start(1000);
        menu->popup(editor->mapToGlobal(QPoint(100, 80))); QTest::qWait(30);
        QVERIFY(menu->grab().save(reviewPath("image-transform-menu-" + suffix))); dismiss.stop(); menu->close();
    }
    void effectsKeepEditableAnnotations() {
        QImage image(320, 220, QImage::Format_ARGB32); image.fill(QColor(45, 110, 180, 115));
        { QPainter painter(&image); painter.fillRect(QRect(20, 20, 110, 160), QColor("#d8a268")); painter.fillRect(QRect(175, 35, 120, 135), QColor("#83bd9b")); }
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(50, 30), QPointF(150, 140)}, QColor("#e04452"), 4, {}});
        canvas->document.add({Tool::Text, {QPointF(175, 55)}, QColor("#2463eb"), 3, QStringLiteral("Editable")});
        canvas->undo(); canvas->redo(); editor->show(); QTest::qWait(40);
        auto *gray = editor->findChild<QAction *>("grayscaleImage"); auto *invert = editor->findChild<QAction *>("invertImage"); QVERIFY(gray); QVERIFY(invert);
        const auto before = canvas->rendered(); auto grayscale = before.copy();
        for (int y = 0; y < grayscale.height(); ++y) for (int x = 0; x < grayscale.width(); ++x) {
            const auto pixel = before.pixel(x, y); const int value = qGray(pixel); grayscale.setPixel(x, y, qRgba(value, value, value, qAlpha(pixel)));
        }
        gray->trigger(); QCOMPARE(canvas->rendered(), grayscale); QCOMPARE(canvas->document.original(), image);
        canvas->undo(); QCOMPARE(canvas->rendered(), before); canvas->redo(); QCOMPARE(canvas->rendered(), grayscale);
        auto invertedGray = grayscale.copy(); invertedGray.invertPixels(QImage::InvertRgb); invert->trigger(); QCOMPARE(canvas->rendered(), invertedGray);
        canvas->undo(); QCOMPARE(canvas->rendered(), grayscale); canvas->undo(); QCOMPARE(canvas->rendered(), before);
        auto inverted = before.copy(); inverted.invertPixels(QImage::InvertRgb); invert->trigger(); QCOMPARE(canvas->rendered(), inverted);
        auto point = [&](QPointF source) {
            const QRectF view = canvas->imageRect();
            return (view.topLeft() + QPointF(source.x() * view.width() / image.width(), source.y() * view.height() / image.height())).toPoint();
        };
        const QColor newColor("#ff6a3d"); canvas->setTool(Tool::Rectangle); canvas->setColor(newColor); canvas->setLineWidth(6);
        QTest::mousePress(canvas, Qt::LeftButton, {}, point(QPointF(190, 175))); QTest::mouseMove(canvas, point(QPointF(280, 205))); QTest::mouseRelease(canvas, Qt::LeftButton, {}, point(QPointF(280, 205)));
        QCOMPARE(canvas->document.annotations().size(), 3); const auto added = canvas->document.annotations()[2];
        const auto colored = canvas->rendered();
        const QPoint stroke(qRound((added.points[0].x() + added.points[1].x()) / 2), qRound(added.points[0].y())); QCOMPARE(colored.pixelColor(stroke), newColor);
        canvas->setSelectionMode(true); const auto bounds = canvas->document.bounds(0); const QPointF edge(bounds.left(), bounds.center().y());
        QCOMPARE(canvas->document.hitTest(edge, 2), 0); const QPoint start = point(edge); const QPoint end = start + QPoint(12, 8);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, start); QCOMPARE(canvas->selectedAnnotation(), 0); const auto old = canvas->document.annotations()[0];
        QTest::mousePress(canvas, Qt::LeftButton, {}, start); QTest::mouseMove(canvas, end); QTest::mouseRelease(canvas, Qt::LeftButton, {}, end);
        QVERIFY(!(canvas->document.annotations()[0] == old)); canvas->undo(); QCOMPARE(canvas->rendered(), colored);
        canvas->undo(); QCOMPARE(canvas->rendered(), inverted); canvas->undo(); QCOMPARE(canvas->rendered(), before);
        canvas->redo(); QCOMPARE(canvas->rendered(), inverted); QCOMPARE(canvas->document.original(), image);
    }
    void imageEffectMenuKeepsKeyboardUsable() {
        QImage image(240, 160, QImage::Format_ARGB32); image.fill(QColor("#3975dc"));
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas); editor->show(); editor->raise(); editor->activateWindow();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitForWindowActive(editor, 15000))
            QSKIP("Native keyboard/focus checks need an unlocked desktop and foreground test app");
#endif
        QTRY_VERIFY(editor->isActiveWindow());
        auto *menu = editor->findChild<QMenu *>("ImageTransformMenu"); auto *invert = editor->findChild<QAction *>("invertImage"); QVERIFY(menu); QVERIFY(invert);
        auto expected = canvas->rendered(); expected.invertPixels(QImage::InvertRgb);
        const auto close = qScopeGuard([&] { menu->close(); }); menu->popup(editor->mapToGlobal(QPoint(100, 80))); QTRY_VERIFY(menu->isVisible());
        menu->setActiveAction(nullptr);
        for (int i = 0; i < 4; ++i) QTest::keyClick(menu, Qt::Key_Down);
        QCOMPARE(menu->activeAction(), invert); QTest::keyClick(menu, Qt::Key_Return);
        QTRY_VERIFY(!menu->isVisible()); QCOMPARE(canvas->rendered(), expected); QCOMPARE(canvas->document.original(), image);
    }
    void colorEffectSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void colorEffectSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QImage image(900, 500, QImage::Format_ARGB32); image.fill(QColor("#f7f9fc"));
        {
            QPainter painter(&image); const QVector<QColor> colors{QColor("#517dc6"), QColor("#dc9a59"), QColor("#6eaa90")};
            QFont heading = painter.font(); heading.setPixelSize(26); painter.setFont(heading); painter.setPen(QColor("#25354d")); painter.drawText(QPoint(60, 65), "Color study · 颜色样本");
            for (int i = 0; i < colors.size(); ++i) { painter.fillRect(QRect(60 + i * 270, 110, 235, 180), colors[i]); painter.fillRect(QRect(60 + i * 270, 310, 235, 28), colors[i].lighter(140)); }
            QFont body = painter.font(); body.setPixelSize(18); painter.setFont(body); painter.setPen(QColor("#536174")); painter.drawText(QPoint(65, 405), "Local image · Editable annotations · Undo available");
        }
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(45, 95), QPointF(310, 350)}, QColor("#df475a"), 4, {}});
        canvas->document.add({Tool::Arrow, {QPointF(460, 375), QPointF(600, 270)}, QColor("#3975dc"), 5, {}});
        canvas->document.add({Tool::Text, {QPointF(610, 355)}, QColor("#3975dc"), 4, QStringLiteral("保留标注 / Editable")});
        canvas->undo(); canvas->redo(); editor->resize(size); editor->show(); QTest::qWait(40);
        auto *gray = editor->findChild<QAction *>("grayscaleImage"); auto *invert = editor->findChild<QAction *>("invertImage"); QVERIFY(gray); QVERIFY(invert);
        const QString suffix = QString::fromLatin1(QTest::currentDataTag()); gray->trigger(); QTest::qWait(20); QVERIFY(editor->grab().save(reviewPath("color-effect-grayscale-" + suffix)));
        canvas->undo(); invert->trigger(); QTest::qWait(20); QVERIFY(editor->grab().save(reviewPath("color-effect-invert-" + suffix)));
        auto *tools = editor->findChild<QToolBar *>("CaptureTools"); QVERIFY(tools); int drawingTools = 0; for (auto *entry : tools->actions()) if (entry->objectName().startsWith("annotationTool-")) ++drawingTools; QCOMPARE(drawingTools, 11);
        auto *menu = editor->findChild<QMenu *>("ImageTransformMenu"); QVERIFY(menu); QCOMPARE(menu->actions().size(), 6); QVERIFY(menu->actions()[2]->isSeparator());
        QStringList names; for (auto *entry : menu->actions()) if (!entry->isSeparator()) names.append(entry->objectName());
        QCOMPARE(names, QStringList({QStringLiteral("flipHorizontal"), QStringLiteral("flipVertical"), QStringLiteral("grayscaleImage"), QStringLiteral("invertImage"), QStringLiteral("brightnessImage")}));
        const auto closeMenu = qScopeGuard([&] { menu->close(); }); QTimer dismiss; dismiss.setSingleShot(true); connect(&dismiss, &QTimer::timeout, menu, &QMenu::close); dismiss.start(1000);
        menu->popup(editor->mapToGlobal(QPoint(100, 80))); QTest::qWait(30); QVERIFY(menu->grab().save(reviewPath("color-effect-menu-" + suffix))); dismiss.stop(); menu->close();
    }
    void brightnessPreviewKeepsEditableAnnotations() {
        QImage image(320, 220, QImage::Format_ARGB32); image.fill(QColor(45, 110, 180, 115));
        { QPainter painter(&image); painter.fillRect(QRect(20, 20, 110, 160), QColor("#d8a268")); painter.fillRect(QRect(175, 35, 120, 135), QColor("#83bd9b")); }
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(50, 30), QPointF(150, 140)}, QColor("#e04452"), 4, {}});
        canvas->document.add({Tool::Text, {QPointF(175, 55)}, QColor("#2463eb"), 3, QStringLiteral("Editable")});
        canvas->undo(); canvas->redo(); editor->show(); QTest::qWait(40);
        auto *brightness = editor->findChild<QAction *>("brightnessImage"); QVERIFY(brightness);
        auto point = [&](QPointF source) {
            const QRectF view = canvas->imageRect();
            return (view.topLeft() + QPointF(source.x() * view.width() / image.width(), source.y() * view.height() / image.height())).toPoint();
        };
        auto adjusted = [](QImage output, int amount) {
            const int offset = qRound(amount * 255 / 100.0);
            for (int y = 0; y < output.height(); ++y) for (int x = 0; x < output.width(); ++x) {
                const auto pixel = output.pixel(x, y);
                output.setPixel(x, y, qRgba(qBound(0, qRed(pixel) + offset, 255), qBound(0, qGreen(pixel) + offset, 255), qBound(0, qBlue(pixel) + offset, 255), qAlpha(pixel)));
            }
            return output;
        };
        auto dialog = [&](int amount, bool apply) {
            const auto before = canvas->rendered(); const auto revision = canvas->document.revision(); const int selected = canvas->selectedAnnotation(); const bool selecting = canvas->selectionMode();
            const auto expected = adjusted(before, amount); bool checked = false; QTimer timer;
            connect(&timer, &QTimer::timeout, this, [&] {
                auto *prompt = editor->findChild<QInputDialog *>("BrightnessDialog"); if (!prompt || !prompt->isVisible()) return;
                timer.stop(); const auto close = qScopeGuard([&] { if (apply) prompt->accept(); else prompt->reject(); });
                QCOMPARE(prompt->inputMode(), QInputDialog::IntInput); QCOMPARE(prompt->intMinimum(), -100); QCOMPARE(prompt->intMaximum(), 100); QCOMPARE(prompt->intStep(), 1); QCOMPARE(prompt->intValue(), 0);
                prompt->setIntValue(amount); canvas->repaint();
                QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.revision(), revision); QCOMPARE(canvas->document.previewBrightness(amount), expected);
                const auto preview = canvas->grab().toImage(); const QPoint sample = point(QPointF(75, 170)); const qreal ratio = preview.devicePixelRatio();
                QCOMPARE(preview.pixelColor(qRound(sample.x() * ratio), qRound(sample.y() * ratio)), expected.pixelColor(75, 170)); checked = true;
            });
            timer.start(10); brightness->trigger(); timer.stop(); QVERIFY(checked);
            if (!apply || amount == 0) { QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.revision(), revision); QCOMPARE(canvas->selectedAnnotation(), selected); QCOMPARE(canvas->selectionMode(), selecting); }
            else { QCOMPARE(canvas->rendered(), expected); QCOMPARE(canvas->document.revision(), revision + 1); }
        };
        const auto before = canvas->rendered(); const auto brightened = adjusted(before, 25); dialog(25, true); QCOMPARE(canvas->rendered(), brightened);
        canvas->undo(); QCOMPARE(canvas->rendered(), before); QVERIFY(canvas->document.canRedo());
        canvas->setSelectionMode(true); QTest::mouseClick(canvas, Qt::LeftButton, {}, point(QPointF(50, 90))); QCOMPARE(canvas->selectedAnnotation(), 0);
        dialog(-30, false); QVERIFY(canvas->document.canRedo()); dialog(0, true); QVERIFY(canvas->document.canRedo());
        canvas->redo(); QCOMPARE(canvas->rendered(), brightened);
        canvas->setSelectionMode(true); const auto bounds = canvas->document.bounds(0); const QPointF edge(bounds.left(), bounds.center().y());
        const QPoint start = point(edge); const QPoint end = start + QPoint(12, 8); QTest::mouseClick(canvas, Qt::LeftButton, {}, start); QCOMPARE(canvas->selectedAnnotation(), 0);
        const auto old = canvas->document.annotations()[0]; QTest::mousePress(canvas, Qt::LeftButton, {}, start); QTest::mouseMove(canvas, end); QTest::mouseRelease(canvas, Qt::LeftButton, {}, end);
        QVERIFY(!(canvas->document.annotations()[0] == old)); canvas->undo(); QCOMPARE(canvas->rendered(), brightened);
        const QColor newColor("#ff6a3d"); canvas->setTool(Tool::Rectangle); canvas->setColor(newColor); canvas->setLineWidth(6);
        QTest::mousePress(canvas, Qt::LeftButton, {}, point(QPointF(190, 175))); QTest::mouseMove(canvas, point(QPointF(280, 205))); QTest::mouseRelease(canvas, Qt::LeftButton, {}, point(QPointF(280, 205)));
        QCOMPARE(canvas->document.annotations().size(), 3); const auto added = canvas->document.annotations()[2]; const QPoint stroke(qRound((added.points[0].x() + added.points[1].x()) / 2), qRound(added.points[0].y())); QCOMPARE(canvas->rendered().pixelColor(stroke), newColor);
        canvas->undo(); QCOMPARE(canvas->rendered(), brightened); canvas->undo(); QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.original(), image);
    }
    void brightnessDialogKeepsNativeKeyboardUsable() {
        QImage image(240, 160, QImage::Format_ARGB32); image.fill(QColor("#3975dc"));
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas); editor->show(); editor->raise(); editor->activateWindow();
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == "cocoa" && !QTest::qWaitForWindowActive(editor, 15000))
            QSKIP("Native keyboard/focus checks need an unlocked desktop and foreground test app");
#endif
        QTRY_VERIFY(editor->isActiveWindow()); canvas->setFocus(); auto *brightness = editor->findChild<QAction *>("brightnessImage"); QVERIFY(brightness);
        const QVector<int> amounts{20, -15, 0};
        for (int i = 0; i < amounts.size(); ++i) {
            bool handled = false; QTimer timer;
            connect(&timer, &QTimer::timeout, this, [&] {
                auto *prompt = editor->findChild<QInputDialog *>("BrightnessDialog"); if (!prompt || !prompt->isVisible()) return;
                timer.stop(); handled = true; prompt->setIntValue(amounts[i]); if (i == 1) prompt->reject(); else prompt->accept();
            });
            timer.start(10); brightness->trigger(); timer.stop(); QVERIFY(handled); QVERIFY(editor->isVisible());
            if (QGuiApplication::platformName() == "offscreen") { editor->activateWindow(); canvas->setFocus(); }
            QTRY_VERIFY(editor->isActiveWindow()); QTRY_VERIFY(canvas->hasFocus());
            QTest::keyClick(canvas, Qt::Key_C); QVERIFY(canvas->cropMode()); QTest::keyClick(canvas, Qt::Key_Escape); QVERIFY(!canvas->cropMode()); QVERIFY(editor->isVisible());
        }
        QCOMPARE(canvas->rendered().pixelColor(0, 0), QColor(108, 168, 255)); QCOMPARE(canvas->document.original(), image);
    }
    void brightnessSnapshots_data() { annotationObjectEditorSnapshots_data(); }
    void brightnessSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap(); const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        QImage image(900, 500, QImage::Format_ARGB32); image.fill(QColor("#e2e8f0"));
        {
            QPainter painter(&image); const QVector<QColor> colors{QColor("#416eae"), QColor("#c78b4e"), QColor("#528e74")};
            QFont font = painter.font(); font.setPixelSize(26); painter.setFont(font); painter.setPen(QColor("#25354d")); painter.drawText(QPoint(60, 65), "Brightness study · 亮度样本");
            for (int i = 0; i < colors.size(); ++i) { painter.fillRect(QRect(60 + i * 270, 110, 235, 180), colors[i]); painter.fillRect(QRect(60 + i * 270, 310, 235, 28), colors[i].lighter(130)); }
        }
        auto *editor = new Editor(image, {}, "en-US"); const auto cleanup = qScopeGuard([&] { delete editor; });
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Rectangle, {QPointF(45, 95), QPointF(310, 350)}, QColor("#df475a"), 4, {}});
        canvas->document.add({Tool::Arrow, {QPointF(460, 375), QPointF(600, 270)}, QColor("#3975dc"), 5, {}});
        canvas->document.add({Tool::Text, {QPointF(610, 355)}, QColor("#3975dc"), 4, QStringLiteral("保留标注 / Editable")});
        canvas->undo(); canvas->redo(); editor->resize(size); editor->show(); QTest::qWait(40);
        auto *brightness = editor->findChild<QAction *>("brightnessImage"); QVERIFY(brightness); const QString suffix = QString::fromLatin1(QTest::currentDataTag());
        const auto before = canvas->rendered(); const auto revision = canvas->document.revision(); bool captured = false; QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *prompt = editor->findChild<QInputDialog *>("BrightnessDialog"); if (!prompt || !prompt->isVisible()) return;
            timer.stop(); const auto apply = qScopeGuard([&] { prompt->accept(); }); prompt->setIntValue(25); canvas->repaint();
            QCOMPARE(prompt->windowTitle(), t("调整亮度")); QCOMPARE(prompt->labelText(), t("负值变暗，正值变亮（-100 至 100）：")); QCOMPARE(prompt->okButtonText(), t("应用")); QCOMPARE(prompt->cancelButtonText(), t("取消"));
            QCOMPARE(canvas->rendered(), before); QCOMPARE(canvas->document.revision(), revision);
            captured = prompt->grab().save(reviewPath("brightness-dialog-" + suffix)) && editor->grab().save(reviewPath("brightness-preview-" + suffix));
        });
        timer.start(10); brightness->trigger(); timer.stop(); QVERIFY(captured); QCOMPARE(canvas->document.revision(), revision + 1);
        QTest::qWait(20); QVERIFY(editor->grab().save(reviewPath("brightness-applied-" + suffix))); QCOMPARE(canvas->document.original(), image);
    }
    void pinEditorAppliesRenderedImageToSource() {
        QImage image(120, 80, QImage::Format_RGB32); image.fill(Qt::white);
        controller_->findChild<ClipboardController *>()->pinImage(image);
        QPointer<Pin> pin;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Pin *>(widget)) pin = candidate;
        QVERIFY(pin);
        const auto cleanup = qScopeGuard([&] { delete pin.data(); });
        pin->editRequested(pin);
        QPointer<Editor> editor;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<Editor *>(widget)) editor = candidate;
        QVERIFY(editor);
        auto *canvas = editor->findChild<Canvas *>(); QVERIFY(canvas);
        canvas->document.add({Tool::Line, {{10, 10}, {100, 60}}, Qt::red, 3, {}});
        const auto expected = canvas->rendered();
        QAction *apply = nullptr;
        for (auto *action : editor->findChildren<QAction *>()) if (action->text() == t("应用并关闭")) apply = action;
        QVERIFY(apply && apply->isVisible());
        apply->trigger();
        QTRY_VERIFY(editor.isNull());
        QCOMPARE(pin->image(), expected);
        QVERIFY(pin->isVisible());
    }
    void pinSessionsRestoreClosedPinsAndSurviveRestart() {
        QTemporaryDir directory;
        const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}, {"pin", QJsonObject{{"restoreOnStartup", false}}}}));
        QList<Pin *> baseline;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget)) baseline.append(pin);
        auto pins = [&] {
            QList<Pin *> result;
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget); pin && !baseline.contains(pin)) result.append(pin);
            return result;
        };
        auto controller = std::make_unique<Application>(nullptr, false);
        auto menu = [&] { return controller->findChild<QSystemTrayIcon *>()->contextMenu(); };
        auto action = [&](const QString &name) { return menu()->findChild<QAction *>(name); };
        auto closeAll = [&] { for (auto *entry : menu()->findChildren<QAction *>()) if (entry->text() == t("关闭全部贴图")) entry->trigger(); };
        QVERIFY(!action("RestoreClosedPin")->isEnabled());
        QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::red);
        controller->findChild<ClipboardController *>()->pinImage(image);
        QCOMPARE(pins().size(), 1); QPointer<Pin> pin = pins().first();
        pin->setImage(image.transformed(QTransform().rotate(90)));
        pin->setWindowFlag(Qt::WindowStaysOnTopHint, false); pin->show();
        pin->setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(50, 60), QSize(240, 180)));
        pin->setWindowOpacity(0.55);
        const auto expected = pin->state();
        pin->close(); QTRY_VERIFY(pin.isNull());
        QVERIFY(action("RestoreClosedPin")->isEnabled()); action("RestoreClosedPin")->trigger();
        QCOMPARE(pins().size(), 1); pin = pins().first();
        QCOMPARE(pin->image(), expected.image); QCOMPARE(pin->geometry(), expected.geometry);
        QCOMPARE(pin->state().id, expected.id); QCOMPARE(pin->state().originalSize, expected.originalSize);
        QVERIFY(qAbs(pin->windowOpacity() - expected.opacity) < 0.01); QVERIFY(!(pin->windowFlags() & Qt::WindowStaysOnTopHint));
        QVERIFY(!action("RestoreClosedPin")->isEnabled());
        image.fill(Qt::green); controller->findChild<ClipboardController *>()->pinImage(image);
        closeAll(); QTRY_VERIFY(pins().isEmpty());
        action("RestoreClosedPin")->trigger(); QCOMPARE(pins().size(), 1); QCOMPARE(pins().first()->image(), image);
        action("RestoreClosedPin")->trigger(); QCOMPARE(pins().size(), 2);
        for (auto *open : pins()) open->hide();
        controller.reset(); QVERIFY(pins().isEmpty());
        PinSession saved(directory.filePath("PinSessions")); QCOMPARE(saved.load().pins.size(), 2);
        const auto originalIndex = readJson(directory.filePath("PinSessions/session.json"));
        controller = std::make_unique<Application>(nullptr, false); QTest::qWait(50);
        QVERIFY(pins().isEmpty()); controller.reset();
        QCOMPARE(readJson(directory.filePath("PinSessions/session.json")), originalIndex);
        controller = std::make_unique<Application>(nullptr, false);
        action("RestorePinSession")->trigger(); action("RestorePinSession")->trigger(); QTRY_COMPARE(pins().size(), 2);
        action("RestorePinSession")->trigger(); QTest::qWait(50); QCOMPARE(pins().size(), 2);
        controller.reset();
        auto settings = readJson(settingsPath()); settings["pin"] = QJsonObject{{"restoreOnStartup", true}}; QVERIFY(writeJson(settingsPath(), settings));
        controller = std::make_unique<Application>(nullptr, false); QTRY_COMPARE(pins().size(), 2);
        bool matched = false;
        for (auto *restored : pins()) if (restored->state().id == expected.id) {
            matched = true; QCOMPARE(restored->image(), expected.image); QCOMPARE(restored->geometry(), expected.geometry);
            QVERIFY(qAbs(restored->windowOpacity() - expected.opacity) < 0.01); QVERIFY(!(restored->windowFlags() & Qt::WindowStaysOnTopHint));
        }
        QVERIFY(matched); closeAll(); QTRY_VERIFY(pins().isEmpty()); controller.reset();
        QVERIFY(saved.load().pins.isEmpty());
        controller = std::make_unique<Application>(nullptr, false); QTest::qWait(50); QVERIFY(pins().isEmpty());
    }
    void directClipboardPinAcceptsTextAndLinks() {
        if (QGuiApplication::platformName() != "offscreen") QSKIP("Uses an isolated offscreen clipboard");
        QList<PinWindow *> baseline;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<PinWindow *>(widget)) baseline.append(pin);
        QList<QPointer<PinWindow>> created;
        const auto cleanup = qScopeGuard([&] { for (const auto &pin : created) if (pin) pin->close(); QApplication::clipboard()->clear(); });
        auto *keys = controller_->findChild<Hotkeys *>(); QVERIFY(keys);
        for (const auto &text : {QStringLiteral("  直接贴出的原文\r\n\t😀  "), QStringLiteral("https://example.com/direct-pin")}) {
            auto *mime = new QMimeData; mime->setText(text);
            if (text.startsWith("https:")) mime->setUrls({QUrl(text)});
            QApplication::clipboard()->setMimeData(mime); keys->activated(3);
            TextPin *match = nullptr;
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<TextPin *>(widget); pin && !baseline.contains(pin) && pin->state().text == text) match = pin;
            QVERIFY(match); created.append(match);
        }
        for (const bool htmlOnly : {false, true}) {
            const QString html = htmlOnly ? QStringLiteral("<b>Only HTML text</b>") : QStringLiteral("<b>HTML and plain text</b>");
            const QString text = htmlOnly ? QStringLiteral("Only HTML text") : QStringLiteral("HTML and plain text");
            auto *mime = new QMimeData; mime->setHtml(html); if (!htmlOnly) mime->setText(text);
            QApplication::clipboard()->setMimeData(mime); keys->activated(3);
            TextPin *match = nullptr;
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<TextPin *>(widget); pin && !baseline.contains(pin) && pin->state().html == html) match = pin;
            QVERIFY(match); QCOMPARE(match->state().text, text); created.append(match);
            auto *editor = match->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
            QCOMPARE(editor->toPlainText(), text); QVERIFY(editor->document()->find(text).charFormat().fontWeight() >= QFont::Bold);
        }
        QImage image(40, 30, QImage::Format_ARGB32); image.fill(Qt::green);
        auto *mime = new QMimeData; mime->setText("An image takes priority"); mime->setImageData(image);
        QApplication::clipboard()->setMimeData(mime); keys->activated(3);
        Pin *picture = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget); pin && !baseline.contains(pin)) picture = pin;
        QVERIFY(picture); QCOMPARE(picture->image(), image); created.append(picture);
        mime = new QMimeData; mime->setUrls({QUrl::fromLocalFile(directory_.filePath("file.txt"))}); mime->setText("file path");
        QApplication::clipboard()->setMimeData(mime);
        bool rejected = false; QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) { rejected = true; box->accept(); }
        });
        dismiss.start(10); keys->activated(3); dismiss.stop(); QVERIFY(rejected);
        int extra = 0; for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<PinWindow *>(widget); pin && !baseline.contains(pin)) ++extra;
        QCOMPARE(extra, created.size());
    }
    void textPinsFromHistoryRestoreWithImages() {
        QTemporaryDir directory;
        const auto previous = qgetenv("STILL_DATA_DIR"); const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}}));
        QList<PinWindow *> baseline;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<PinWindow *>(widget)) baseline.append(pin);
        auto pins = [&] {
            QList<PinWindow *> result;
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<PinWindow *>(widget); pin && !baseline.contains(pin)) result.append(pin);
            return result;
        };
        auto controller = std::make_unique<Application>(nullptr, false); controller->openClipboard();
        MainWindow *window = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<MainWindow *>(widget); candidate && candidate != window_) window = candidate;
        QVERIFY(window);
        auto *archive = controller->findChild<ClipboardArchive *>(); QVERIFY(archive);
        const QString plain = QStringLiteral("  <b>字面文字</b>\r\n\t😀  ");
        const QString rich = QStringLiteral("富文本的纯文本\n只保留文字");
        const QString richHtml = QStringLiteral("<b>富文本的纯文本</b><br>只保留文字");
        const QString link = QStringLiteral("https://example.com/text-pin");
        for (const auto &text : {plain, rich, link}) {
            QMimeData mime; mime.setText(text);
            if (text == rich) mime.setHtml(richHtml);
            archive->add(captureClipboard(mime));
            QTRY_VERIFY(!window->clips().isEmpty());
            QTRY_VERIFY(window->clips().first().toMap().value("preview").toString().contains(text.trimmed().left(5)));
            window->setSelectedIndex(0); window->activate("pin");
            QTRY_COMPARE(pins().size(), text == plain ? 1 : text == rich ? 2 : 3);
            bool matched = false; for (auto *pin : pins()) if (pin->state().text == text) {
                matched = true; QVERIFY(qobject_cast<TextPin *>(pin)); QCOMPARE(pin->state().html, text == rich ? richHtml : QString());
                if (text == rich) { auto *editor = pin->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor); QVERIFY(editor->document()->find(QStringLiteral("富文本")).charFormat().fontWeight() >= QFont::Bold); }
            }
            QVERIFY(matched);
        }
        QPointer<PinWindow> textPin;
        for (auto *pin : pins()) if (pin->state().text == plain) textPin = pin;
        QVERIFY(textPin); textPin->setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(60, 70), QSize(360, 260)));
        textPin->setWindowOpacity(0.65); const auto expected = textPin->state(); textPin->close(); QTRY_VERIFY(textPin.isNull());
        auto menu = [&] { return controller->findChild<QSystemTrayIcon *>()->contextMenu(); };
        menu()->findChild<QAction *>("RestoreClosedPin")->trigger(); QTRY_COMPARE(pins().size(), 3);
        bool matched = false;
        for (auto *pin : pins()) if (pin->state().id == expected.id) { matched = true; QCOMPARE(pin->state().text, plain); QCOMPARE(pin->geometry(), expected.geometry); }
        QVERIFY(matched);
        QPointer<PinWindow> richPin;
        for (auto *pin : pins()) if (pin->state().html == richHtml) richPin = pin;
        QVERIFY(richPin); const auto richId = richPin->state().id; richPin->close(); QTRY_VERIFY(richPin.isNull());
        menu()->findChild<QAction *>("RestoreClosedPin")->trigger(); QTRY_COMPARE(pins().size(), 3);
        bool matchedRich = false;
        for (auto *pin : pins()) if (pin->state().id == richId) {
            matchedRich = true; QCOMPARE(pin->state().text, rich); QCOMPARE(pin->state().html, richHtml);
            auto *editor = pin->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor); QVERIFY(editor->document()->find(QStringLiteral("富文本")).charFormat().fontWeight() >= QFont::Bold);
        }
        QVERIFY(matchedRich);
        QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::green);
        controller->findChild<ClipboardController *>()->pinImage(image); QCOMPARE(pins().size(), 4);
        controller.reset(); QVERIFY(pins().isEmpty());
        PinSession saved(directory.filePath("PinSessions")); const auto session = saved.load(); QVERIFY(session.errors.isEmpty()); QCOMPARE(session.pins.size(), 4);
        controller = std::make_unique<Application>(nullptr, false);
        menu()->findChild<QAction *>("RestorePinSession")->trigger(); QTRY_COMPARE(pins().size(), 4);
        matched = false; matchedRich = false; bool foundImage = false;
        for (auto *pin : pins()) {
            if (pin->state().id == expected.id) {
                matched = true; QCOMPARE(pin->state().text, plain); QTRY_COMPARE(pin->geometry(), expected.geometry);
                QVERIFY(qAbs(pin->windowOpacity() - expected.opacity) < 0.01);
            }
            if (auto *picture = qobject_cast<Pin *>(pin)) { foundImage = true; QCOMPARE(picture->image(), image); }
            if (pin->state().id == richId) {
                matchedRich = true; QCOMPARE(pin->state().text, rich); QCOMPARE(pin->state().html, richHtml);
                auto *editor = pin->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor); QVERIFY(editor->document()->find(QStringLiteral("富文本")).charFormat().fontWeight() >= QFont::Bold);
            }
        }
        QVERIFY(matched); QVERIFY(matchedRich); QVERIFY(foundImage);
    }
    void textPinSnapshots_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme"); QTest::addColumn<QSize>("size");
        QTest::newRow("normal-light-zh") << QString("zh_CN") << QString("light") << QSize(400, 320);
        QTest::newRow("normal-dark-en") << QString("en") << QString("dark") << QSize(400, 320);
        QTest::newRow("compact-light-en") << QString("en") << QString("light") << QSize(280, 200);
        QTest::newRow("compact-dark-zh") << QString("zh_CN") << QString("dark") << QSize(280, 200);
    }
    void textPinSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        auto *pin = new TextPin(QStringLiteral("项目记录 · Project notes\n<b>这是文字，不加载 HTML</b>\n\t保留缩进、空格与 emoji 😀\n\n") + QStringLiteral("可滚动查看长文本 · Scroll to read more\n").repeated(40));
        const auto cleanup = qScopeGuard([&] { delete pin; });
        pin->resize(size); pin->show(); QTest::qWait(40);
        const QString suffix = QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(pin->grab().save(reviewPath("text-pin-" + suffix)));
        auto *editor = pin->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
        QVERIFY(editor->verticalScrollBar()->maximum() > 0); editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
        QTest::qWait(20); QVERIFY(pin->grab().save(reviewPath("text-pin-scrolled-" + suffix)));
        bool captured = false; QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            auto *menu = pin->findChild<QMenu *>("TextPinMenu"); if (!menu || !menu->isVisible()) return;
            dismiss.stop(); const auto close = qScopeGuard([&] { menu->close(); });
            QStringList primary; for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text());
            QCOMPARE(primary, QStringList({t("复制"), t("全选"), t("更多操作"), t("关闭")}));
            captured = menu->grab().save(reviewPath("text-pin-menu-" + suffix));
        });
        dismiss.start(10); QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), pin->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(pin, &event); QVERIFY(captured);
    }
    void htmlPinSnapshots_data() { textPinSnapshots_data(); }
    void htmlPinSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        const QString html = QStringLiteral("<h3>项目记录 · Project notes</h3><p><b>重点内容 · Key points</b><br><i>保留格式 · Keep formatting</i></p>"
            "<ul><li>文字可选择 · Select text</li><li>长内容可滚动 · Scroll to read</li></ul>"
            "<table border='1' cellspacing='0' cellpadding='4'><tr><th>事项 · Item</th><th>状态 · Status</th></tr><tr><td>本机展示</td><td>Ready</td></tr></table>"
            "<pre>const note = '😀';\n  keepIndent();</pre>") + QStringLiteral("<p>格式化文字仍可滚动 · Formatted text can scroll.</p>").repeated(30);
        QTextDocument source; source.setHtml(html);
        auto *pin = new TextPin(source.toPlainText(), html); const auto cleanup = qScopeGuard([&] { delete pin; });
        pin->resize(size); pin->show(); pin->activateWindow();
        auto *editor = pin->findChild<QTextEdit *>("TextPinContent"); QVERIFY(editor);
        auto selection = editor->document()->find(QStringLiteral("重点内容")); QVERIFY(!selection.isNull()); editor->setTextCursor(selection); editor->setFocus();
        QTest::qWait(40); const QString suffix = QString::fromLatin1(QTest::currentDataTag());
        QVERIFY(pin->grab().save(reviewPath("html-pin-" + suffix)));
        QVERIFY(editor->verticalScrollBar()->maximum() > 0); editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
        QTest::qWait(20); QVERIFY(pin->grab().save(reviewPath("html-pin-scrolled-" + suffix)));
        bool captured = false; QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            auto *menu = pin->findChild<QMenu *>("TextPinMenu"); if (!menu || !menu->isVisible()) return;
            dismiss.stop(); const auto close = qScopeGuard([&] { menu->close(); });
            QStringList primary; for (auto *action : menu->actions()) if (!action->isSeparator()) primary.append(action->text());
            QCOMPARE(primary, QStringList({t("复制"), t("全选"), t("更多操作"), t("关闭")}));
            captured = menu->grab().save(reviewPath("html-pin-menu-" + suffix));
        });
        dismiss.start(10); QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), pin->mapToGlobal(QPoint(20, 20)));
        QApplication::sendEvent(pin, &event); QVERIFY(captured);
    }
    void restoredPinUsesTheSavedScreen_data() {
        QTest::addColumn<int>("screenIndex");
        for (int i = 0; i < QGuiApplication::screens().size(); ++i) QTest::newRow(qPrintable(QString("screen-%1").arg(i))) << i;
    }
    void restoredPinUsesTheSavedScreen() {
        QFETCH(int, screenIndex);
        const auto screens = QGuiApplication::screens();
        auto *target = screens[screenIndex]; auto *previous = screens[(screenIndex + 1) % screens.size()];
        const QRect desired(target->availableGeometry().topLeft() + QPoint(50, 60), QSize(240, 180));
        QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::white);
        for (int i = 0; i < 15; ++i) {
            auto *pin = new Pin(image, QRect(previous->availableGeometry().topLeft() + QPoint(50, 60), QSize(120, 80)), {}, "en-US");
            const auto cleanup = qScopeGuard([&] { delete pin; });
            pin->setScreen(previous);
            auto state = pin->state(); state.geometry = desired;
            pin->restoreLayout(state); pin->show(); QTest::qWait(30);
            QCOMPARE(pin->geometry(), desired); QCOMPARE(pin->screen(), target);
        }
    }
    void actualQuitKeepsOpenPinsInTheSavedSession() {
        QTemporaryDir directory;
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(directory.filePath("qt-settings.json"), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}}));
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("STILL_DATA_DIR", directory.path());
        process.setProcessEnvironment(environment); process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QCoreApplication::applicationFilePath(), {"--pin-quit-probe"});
        QVERIFY2(process.waitForFinished(15000), qPrintable(process.errorString()));
        const auto output = process.readAll(); QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, output.constData());
        const auto restored = PinSession(directory.filePath("PinSessions")).load();
        QVERIFY(restored.errors.isEmpty()); QCOMPARE(restored.pins.size(), 3);
        bool foundImage = false; bool foundText = false; bool foundHtml = false;
        for (const auto &pin : restored.pins) {
            if (!pin.html.isEmpty()) { foundHtml = true; QCOMPARE(pin.text, QStringLiteral("退出后恢复富文本")); QCOMPARE(pin.html, QStringLiteral("<b>退出后恢复富文本</b>")); }
            else if (pin.image.isNull()) { foundText = true; QCOMPARE(pin.text, QStringLiteral("  退出后恢复文字\r\n\t😀  ")); }
            else {
                foundImage = true; QCOMPARE(pin.image.size(), QSize(80, 120)); QCOMPARE(pin.image.pixelColor(0, 0), QColor(Qt::green));
                QCOMPARE(pin.geometry.size(), QSize(240, 180)); QVERIFY(qAbs(pin.opacity - 0.6) < 0.01); QVERIFY(!pin.alwaysOnTop);
            }
        }
        QVERIFY(foundImage); QVERIFY(foundText); QVERIFY(foundHtml);
    }
    void incompletePinRestorePreservesTheOriginalSession_data() {
        QTest::addColumn<bool>("missing"); QTest::newRow("missing-image") << true; QTest::newRow("pin-limit") << false;
    }
    void incompletePinRestorePreservesTheOriginalSession() {
        QFETCH(bool, missing);
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR");
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"pin", QJsonObject{{"maxPins", missing ? 60 : 1}}}}));
        QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::red);
        const PinState first{QUuid::createUuid().toString(QUuid::WithoutBraces), image, QRect(50, 60, 240, 180), QSize(126, 108), 1, true, {}};
        auto second = first; second.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        PinSession session(directory.filePath("PinSessions")); QString error; QVERIFY(session.save({first, second}, &error));
        const auto manifest = readJson(directory.filePath("PinSessions/session.json"));
        if (missing) QVERIFY(QFile::remove(directory.filePath("PinSessions/" + manifest.value("pins").toArray()[0].toObject().value("image").toString())));
        QString message;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                message = box->text(); box->accept();
            }
        });
        dismiss.start(10);
        auto controller = std::make_unique<Application>(nullptr, false);
        controller->findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>("RestorePinSession")->trigger();
        if (QGuiApplication::platformName() == "offscreen") QTRY_VERIFY(!message.isEmpty());
        QTRY_VERIFY(!controller->findChild<QObject *>("PinSessionRestoreJob"));
        dismiss.stop();
        if (QGuiApplication::platformName() == "offscreen") QVERIFY(message.contains(missing ? t("无法恢复贴图：") : t("已达到贴图数量上限，部分贴图尚未恢复，原会话将保留")));
        controller.reset(); QCOMPARE(readJson(directory.filePath("PinSessions/session.json")), manifest);
        QVERIFY(QFileInfo::exists(directory.filePath("PinSessions/" + manifest.value("pins").toArray()[1].toObject().value("image").toString())));
    }
    void pendingPinRestorePreservesTheOriginalSessionOnExit() {
        QTemporaryDir directory; const auto previous = qgetenv("STILL_DATA_DIR");
        const auto appearance = window_->form().value("appearance").toMap();
        const auto reset = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); InterfacePreferences::instance().apply(QJsonObject::fromVariantMap(appearance)); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys; for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}, {"history", QJsonObject{{"enabled", false}}}}));
        QImage oldImage(120, 80, QImage::Format_ARGB32); oldImage.fill(Qt::red);
        const PinState oldPin{QUuid::createUuid().toString(QUuid::WithoutBraces), oldImage, QRect(50, 60, 240, 180), QSize(126, 108), 1, true, {}};
        PinSession session(directory.filePath("PinSessions")); QString error; QVERIFY(session.save({oldPin}, &error));
        QFile manifest(directory.filePath("PinSessions/session.json")); QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto original = manifest.readAll(); manifest.close();
        const auto record = QJsonDocument::fromJson(original).object().value("pins").toArray().first().toObject();
        const auto oldPath = directory.filePath("PinSessions/" + record.value("image").toString());
        auto controller = std::make_unique<Application>(nullptr, false);
        controller->findChild<QSystemTrayIcon *>()->contextMenu()->findChild<QAction *>("RestorePinSession")->trigger();
        QVERIFY(controller->findChild<QObject *>("PinSessionRestoreJob"));
        QImage newImage(80, 60, QImage::Format_ARGB32); newImage.fill(Qt::green);
        controller->findChild<ClipboardController *>()->pinImage(newImage);
        // Exit before dispatching the queued restore completion, even if its worker has finished.
        controller.reset();
        QVERIFY(manifest.open(QIODevice::ReadOnly)); QCOMPARE(manifest.readAll(), original);
        QCOMPARE(QImage(oldPath).convertToFormat(QImage::Format_ARGB32), oldImage);
        const auto restored = session.load(); QVERIFY(restored.errors.isEmpty()); QCOMPARE(restored.pins.size(), 1);
        QCOMPARE(restored.pins.first().id, oldPin.id);
    }
    void applicationExitFinishesQueuedSettings_data() {
        QTest::addColumn<bool>("withDraft");
        QTest::newRow("queued") << false;
        QTest::newRow("queued-and-draft") << true;
    }
    void applicationExitFinishesQueuedSettings() {
        QFETCH(bool, withDraft);
        QTemporaryDir directory;
        const auto previous = qgetenv("STILL_DATA_DIR");
        const auto restore = qScopeGuard([&] { qputenv("STILL_DATA_DIR", previous); });
        qputenv("STILL_DATA_DIR", directory.path().toUtf8());
        QJsonObject keys;
        for (const auto &name : hotkeyNames) keys[name] = "";
        QVERIFY(writeJson(settingsPath(), {{"hotkeys", keys}}));
        {
            Application controller(nullptr, false);
            if (withDraft) {
                controller.openSettings();
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *window = qobject_cast<MainWindow *>(widget); window && window != window_)
                        window->setDraft("save", "dir", directory.filePath("exports"));
            }
            auto *settings = controller.findChild<SettingsApply *>(); QVERIFY(settings);
            settings->apply({{"ocr", QJsonObject{{"language", "en-US"}}}});
            settings->apply({{"ocr", QJsonObject{{"language", "de-DE"}}}});
            settings->apply({{"clipboard", QJsonObject{{"enabled", false}}}});
            QVERIFY(settings->busy());
        }
        const auto saved = readJson(settingsPath());
        QCOMPARE(saved.value("ocr").toObject().value("language").toString(), QString("de-DE"));
        QVERIFY(!saved.value("clipboard").toObject().value("enabled").toBool(true));
        if (withDraft) QCOMPARE(saved.value("save").toObject().value("dir").toString(), directory.filePath("exports"));
    }
    void cleanupTestCase() {
        delete controller_;
        if (previousDirectory_.isNull()) qunsetenv("STILL_DATA_DIR"); else qputenv("STILL_DATA_DIR", previousDirectory_);
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
#ifdef Q_OS_MACOS
    if (app.arguments().contains("--delay-activation-target")) {
        QLabel target(QStringLiteral("Synthetic activation target")); target.setWindowTitle("Still delay activation target"); target.setAlignment(Qt::AlignCenter); target.resize(320, 180);
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        target.show(); QTimer activation; activation.setInterval(100);
        QObject::connect(&activation, &QTimer::timeout, &app, [&] {
            if (@available(macOS 14.0, *)) { [NSApp activate]; if (NSApp.active) activation.stop(); }
        });
        activation.start(); return app.exec();
    }
#endif
    if (app.arguments().contains("--pin-quit-probe")) {
        app.setQuitOnLastWindowClosed(false);
        Application controller(nullptr, false);
        QTimer::singleShot(0, &controller, [&] {
            QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::red);
            auto *clipboard = controller.findChild<ClipboardController *>(); clipboard->pinImage(image);
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget)) pin->close();
            image.fill(Qt::green); clipboard->pinImage(image.transformed(QTransform().rotate(90)));
            clipboard->pinText(QStringLiteral("  退出后恢复文字\r\n\t😀  "));
            clipboard->pinText(QStringLiteral("退出后恢复富文本"), QStringLiteral("<b>退出后恢复富文本</b>"));
            for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<Pin *>(widget); pin && pin->isVisible()) {
                pin->setWindowFlag(Qt::WindowStaysOnTopHint, false); pin->show(); pin->setWindowOpacity(0.6);
                pin->setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft() + QPoint(50, 60), QSize(240, 180)));
            }
            QTimer::singleShot(30, &app, &QApplication::quit);
        });
        return app.exec();
    }
    SettingsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "settings_test.moc"
