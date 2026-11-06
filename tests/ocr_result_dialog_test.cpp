#include "imaging/ocr_result_dialog.h"
#include "imaging/api_proofread_dialog.h"
#include "api_credentials_fixture.h"
#include "api_test_server.h"
#include "settings/api_profile.h"
#include "storage/store.h"
#include "ui/i18n.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QMimeData>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTemporaryDir>
#include <QTextDocument>
#include <memory>

using namespace still;

static QPlainTextEdit *draft(OcrResultDialog &dialog) { return dialog.findChild<QPlainTextEdit *>("OcrEditedText"); }
static QPlainTextEdit *original(OcrResultDialog &dialog) { return dialog.findChild<QPlainTextEdit *>("OcrOriginalText"); }
static QComboBox *lines(OcrResultDialog &dialog) { return dialog.findChild<QComboBox *>("OcrLineBreaks"); }
static QComboBox *spaces(OcrResultDialog &dialog) { return dialog.findChild<QComboBox *>("OcrSpaces"); }
static QTabWidget *tabs(OcrResultDialog &dialog) { return dialog.findChild<QTabWidget *>("OcrResultTabs"); }
static QPushButton *button(OcrResultDialog &dialog, const char *name) { return dialog.findChild<QPushButton *>(name); }
static QString copied(OcrResultDialog &dialog) {
    button(dialog, "OcrCopyText")->click(); return QApplication::clipboard()->text();
}

class OcrResultDialogTest : public QObject {
    Q_OBJECT
    QTemporaryDir data_;
    QByteArray previousData_;
    bool hadData_ = false;
private slots:
    void initTestCase() {
        QVERIFY(data_.isValid()); hadData_ = qEnvironmentVariableIsSet("STILL_DATA_DIR"); previousData_ = qgetenv("STILL_DATA_DIR");
        qputenv("STILL_DATA_DIR", data_.path().toUtf8()); QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    }
    void cleanupTestCase() { if (hadData_) qputenv("STILL_DATA_DIR", previousData_); else qunsetenv("STILL_DATA_DIR"); }
    void init() {
        QFile::remove(apiProfilePath()); credential_fixture::reset();
        InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", "light"}});
        QApplication::clipboard()->clear();
    }
    void defaultPreservesTheOriginalPlainText() {
        const QString raw = QStringLiteral("  Wide  \tQt\r\n中🙂\u00a0A\u2028B\u2029C\rD\n  ");
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        QVERIFY(draft(*dialog)); QVERIFY(original(*dialog)); QVERIFY(lines(*dialog)); QVERIFY(spaces(*dialog)); QVERIFY(tabs(*dialog));
        QCOMPARE(dialog->windowModality(), Qt::NonModal); QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose));
        QVERIFY(dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint)); QVERIFY(original(*dialog)->isReadOnly());
        QVERIFY(!draft(*dialog)->isReadOnly()); QCOMPARE(draft(*dialog)->lineWrapMode(), QPlainTextEdit::NoWrap);
        QCOMPARE(lines(*dialog)->currentIndex(), 0); QCOMPARE(spaces(*dialog)->currentIndex(), 0);
        QVERIFY(!button(*dialog, "OcrApplyFormatting")->isEnabled()); QVERIFY(!button(*dialog, "OcrRestoreOriginal")->isEnabled());
        QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
        QCOMPARE(copied(*dialog), raw);
        tabs(*dialog)->setCurrentIndex(1); QCOMPARE(copied(*dialog), raw);
        original(*dialog)->selectAll(); original(*dialog)->copy(); QCOMPARE(QApplication::clipboard()->text(), raw);
        QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
    }
    void formattingIsExplicitAndUndoable_data() {
        QTest::addColumn<int>("lineMode"); QTest::addColumn<int>("spaceMode"); QTest::addColumn<QString>("expected");
        QTest::newRow("keep-both") << 0 << 0 << QString("  A \t B\r\n C\tD \nE  ");
        QTest::newRow("keep-lines-collapse-spaces") << 0 << 1 << QString(" A B\r\n C D \nE ");
        QTest::newRow("keep-lines-remove-spaces") << 0 << 2 << QString("AB\r\nCD\nE");
        QTest::newRow("space-lines-keep-spaces") << 1 << 0 << QString("  A \t B  C\tD  E  ");
        QTest::newRow("space-lines-collapse-spaces") << 1 << 1 << QString(" A B C D E ");
        QTest::newRow("space-lines-remove-spaces") << 1 << 2 << QString("ABCDE");
        QTest::newRow("remove-lines-keep-spaces") << 2 << 0 << QString("  A \t B C\tD E  ");
        QTest::newRow("remove-lines-collapse-spaces") << 2 << 1 << QString(" A B C D E ");
        QTest::newRow("remove-both") << 2 << 2 << QString("ABCDE");
    }
    void formattingIsExplicitAndUndoable() {
        QFETCH(int, lineMode); QFETCH(int, spaceMode); QFETCH(QString, expected);
        const QString raw = "  A \t B\r\n C\tD \nE  ";
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        lines(*dialog)->setCurrentIndex(lineMode); spaces(*dialog)->setCurrentIndex(spaceMode);
        QCOMPARE(copied(*dialog), raw); QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
        button(*dialog, "OcrApplyFormatting")->click(); QCOMPARE(copied(*dialog), expected);
        if (lineMode || spaceMode) {
            QVERIFY(draft(*dialog)->document()->isUndoAvailable()); draft(*dialog)->undo();
            QCOMPARE(copied(*dialog), raw); QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
            draft(*dialog)->redo(); QCOMPARE(copied(*dialog), expected);
        } else QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
        tabs(*dialog)->setCurrentIndex(1); QCOMPARE(copied(*dialog), raw);
        QVERIFY(!button(*dialog, "OcrApplyFormatting")->isEnabled()); QVERIFY(!lines(*dialog)->isEnabled()); QVERIFY(!spaces(*dialog)->isEnabled());
    }
    void unicodeSpacesAndLineSeparatorsKeepCharactersIntact() {
        const QString raw = QStringLiteral("中🙂\u00a0\t\u2009\u3000甲\u2028乙\u2029丙\u0085丁\r戊\r\n己\n庚");
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        lines(*dialog)->setCurrentIndex(1); spaces(*dialog)->setCurrentIndex(1);
        button(*dialog, "OcrApplyFormatting")->click(); QCOMPARE(copied(*dialog), QStringLiteral("中🙂 甲 乙 丙 丁 戊 己 庚"));
        draft(*dialog)->undo(); QCOMPARE(copied(*dialog), raw);
        lines(*dialog)->setCurrentIndex(2); spaces(*dialog)->setCurrentIndex(2);
        button(*dialog, "OcrApplyFormatting")->click(); QCOMPARE(copied(*dialog), QStringLiteral("中🙂甲乙丙丁戊己庚"));
    }
    void explicitFormattingChangesNormalizedSpacesAndKeepsOriginalLineEndings() {
        const QString raw = QStringLiteral("A\u00a0B\r\n C\t D");
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        spaces(*dialog)->setCurrentIndex(1); button(*dialog, "OcrApplyFormatting")->click();
        QCOMPARE(copied(*dialog), QString("A B\r\n C D"));
        draft(*dialog)->undo(); QCOMPARE(copied(*dialog), raw);
        draft(*dialog)->redo(); QCOMPARE(copied(*dialog), QString("A B\r\n C D"));
        button(*dialog, "OcrRestoreOriginal")->click(); QCOMPARE(copied(*dialog), raw);
        draft(*dialog)->undo(); QCOMPARE(copied(*dialog), QString("A B\r\n C D"));
        draft(*dialog)->redo(); QCOMPARE(copied(*dialog), raw);
    }
    void replacingWithTheSameDisplayedTextReallyChangesTheDraft() {
        const QString raw = QStringLiteral("A\u00a0B");
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        auto *edit = draft(*dialog); edit->selectAll(); edit->insertPlainText("A B");
        auto cursor = edit->textCursor(); cursor.clearSelection(); edit->setTextCursor(cursor);
        QCOMPARE(copied(*dialog), QString("A B"));
        edit->undo(); QCOMPARE(copied(*dialog), raw); edit->redo(); QCOMPARE(copied(*dialog), QString("A B"));
    }
    void formattingUsesTheCurrentCorrectionAndRestorationCanBeUndone() {
        const QString raw = "A\r\n  B";
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        auto *edit = draft(*dialog); edit->moveCursor(QTextCursor::End); edit->insertPlainText("\n  C");
        QCOMPARE(copied(*dialog), QString("A\n  B\n  C"));
        lines(*dialog)->setCurrentIndex(1); spaces(*dialog)->setCurrentIndex(1);
        button(*dialog, "OcrApplyFormatting")->click(); QCOMPARE(copied(*dialog), QString("A B C"));
        edit->undo(); QCOMPARE(copied(*dialog), QString("A\n  B\n  C"));
        edit->redo(); QCOMPARE(copied(*dialog), QString("A B C"));
        tabs(*dialog)->setCurrentIndex(1); button(*dialog, "OcrRestoreOriginal")->click();
        QCOMPARE(tabs(*dialog)->currentIndex(), 0); QCOMPARE(copied(*dialog), raw);
        QVERIFY(!button(*dialog, "OcrRestoreOriginal")->isEnabled()); edit->undo();
        QCOMPARE(copied(*dialog), QString("A B C")); edit->redo(); QCOMPARE(copied(*dialog), raw);
    }
    void copyingSelectionAndWholeTextOnlyProvidesPlainText() {
        const QString raw = QStringLiteral("A🙂B\r\n C");
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        for (int page : {0, 1}) {
            tabs(*dialog)->setCurrentIndex(page);
            auto *edit = page ? original(*dialog) : draft(*dialog);
            auto cursor = edit->textCursor(); cursor.setPosition(1); cursor.setPosition(3, QTextCursor::KeepAnchor); edit->setTextCursor(cursor);
            QCOMPARE(button(*dialog, "OcrCopyText")->text(), t("复制选中文字")); QCOMPARE(copied(*dialog), QStringLiteral("🙂"));
            QCOMPARE(QApplication::clipboard()->mimeData()->formats(), QStringList{"text/plain"});
            edit->copy(); QCOMPARE(QApplication::clipboard()->mimeData()->formats(), QStringList{"text/plain"});
            edit->selectAll(); edit->copy(); QCOMPARE(QApplication::clipboard()->text(), raw);
            cursor = edit->textCursor(); cursor.clearSelection(); edit->setTextCursor(cursor);
            QCOMPARE(button(*dialog, "OcrCopyText")->text(), t("复制全部文字")); QCOMPARE(copied(*dialog), raw);
        }
    }
    void pastedHtmlUsesItsPlainTextWithoutFormatting() {
        auto dialog = std::make_unique<OcrResultDialog>(QString());
        auto *mime = new QMimeData; mime->setText("<tag> text"); mime->setHtml("<b>different bold text</b>");
        QApplication::clipboard()->setMimeData(mime); draft(*dialog)->paste();
        QCOMPARE(draft(*dialog)->toPlainText(), QString("<tag> text"));
        draft(*dialog)->selectAll(); draft(*dialog)->copy();
        QCOMPARE(QApplication::clipboard()->mimeData()->formats(), QStringList{"text/plain"});
        QCOMPARE(QApplication::clipboard()->text(), QString("<tag> text"));
    }
    void languageAndThemeOnlyChangeTheInterface() {
        const QString raw = QStringLiteral("复制\r\nA  B");
        auto dialog = std::make_unique<OcrResultDialog>(raw);
        draft(*dialog)->moveCursor(QTextCursor::End); draft(*dialog)->insertPlainText(" corrected");
        lines(*dialog)->setCurrentIndex(2); spaces(*dialog)->setCurrentIndex(1);
        auto cursor = draft(*dialog)->textCursor(); cursor.setPosition(0); cursor.setPosition(2, QTextCursor::KeepAnchor); draft(*dialog)->setTextCursor(cursor);
        tabs(*dialog)->setCurrentIndex(1); const auto text = draft(*dialog)->toPlainText();
        InterfacePreferences::instance().apply({{"language", "en"}, {"theme", "dark"}});
        QCOMPARE(dialog->windowTitle(), t("识别结果")); QCOMPARE(tabs(*dialog)->currentIndex(), 1);
        QCOMPARE(lines(*dialog)->currentIndex(), 2); QCOMPARE(spaces(*dialog)->currentIndex(), 1);
        QCOMPARE(draft(*dialog)->toPlainText(), text); QCOMPARE(draft(*dialog)->textCursor().selectedText(), QStringLiteral("复制"));
        QVERIFY(draft(*dialog)->document()->isUndoAvailable()); QCOMPARE(copied(*dialog), raw);
        tabs(*dialog)->setCurrentIndex(0); draft(*dialog)->undo();
        cursor = draft(*dialog)->textCursor(); cursor.clearSelection(); draft(*dialog)->setTextCursor(cursor);
        QCOMPARE(copied(*dialog), raw);
        InterfacePreferences::instance().apply({{"language", "zh_CN"}, {"theme", "light"}});
        QCOMPARE(dialog->windowTitle(), QStringLiteral("识别结果")); QCOMPARE(lines(*dialog)->currentIndex(), 2);
    }
    void emptyResultDoesNotOverwriteTheClipboard() {
        auto dialog = std::make_unique<OcrResultDialog>(QString());
        QApplication::clipboard()->setText("keep");
        QVERIFY(!button(*dialog, "OcrCopyText")->isEnabled());
        QVERIFY(!button(*dialog, "OcrRestoreOriginal")->isEnabled());
        lines(*dialog)->setCurrentIndex(1); QVERIFY(!button(*dialog, "OcrApplyFormatting")->isEnabled());
        button(*dialog, "OcrCopyText")->click(); QCOMPARE(QApplication::clipboard()->text(), QString("keep"));
        QVERIFY(!button(*dialog, "OcrApiProofread")->isEnabled());
    }
    void apiAppliesOnlyTheExplicitSnapshotAndCanUndo_data() {
        QTest::addColumn<QString>("prefix"); QTest::addColumn<QString>("selected"); QTest::addColumn<QString>("suffix"); QTest::addColumn<bool>("select");
        QTest::newRow("selection-crlf-unicode") << QStringLiteral("before\r\n\u00a0") << QStringLiteral("sel\r\n中😀\u2028x") << QStringLiteral("\r\n\tunchanged\u2029尾") << true;
        QTest::newRow("selection-separators") << QStringLiteral("before\r\u2029") << QStringLiteral("sel\n\u0085\u2029x") << QStringLiteral("\r\nunchanged") << true;
        QTest::newRow("whole") << QString() << QStringLiteral("whole\r\n中😀\t  text\u00a0") << QString() << false;
    }
    void apiAppliesOnlyTheExplicitSnapshotAndCanUndo() {
        QFETCH(QString, prefix); QFETCH(QString, selected); QFETCH(QString, suffix); QFETCH(bool, select);
        ApiTestServer service; QVERIFY(service.server.isListening()); QString error;
        QVERIFY2(saveApiProfile({service.endpoint(), "fixture-model"}, &error), qPrintable(error));
        credential_fixture::keys[apiCredentialAccount(service.endpoint())] = "fixture-key";
        const QString raw = prefix + selected + suffix;
        auto dialog = std::make_unique<OcrResultDialog>(raw); dialog->show();
        if (select) {
            auto cursor = draft(*dialog)->textCursor();
            const int start = draft(*dialog)->toPlainText().indexOf("sel");
            QString documentSelection = selected; documentSelection.replace("\r\n", "\n");
            cursor.setPosition(start); cursor.setPosition(start + documentSelection.size(), QTextCursor::KeepAnchor); draft(*dialog)->setTextCursor(cursor);
        }
        QApplication::clipboard()->setText("keep"); button(*dialog, "OcrApiProofread")->click();
        QPointer<ApiProofreadDialog> api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api);
        QVERIFY(api->isVisible()); QCOMPARE(api->windowModality(), Qt::WindowModal);
        QVERIFY(service.requests.isEmpty());
        auto *preview = api->findChild<QPlainTextEdit *>("ApiProofreadPreview"); QVERIFY(preview);
        auto *apply = api->findChild<QPushButton *>("ApiProofreadApply"); QVERIFY(apply); QVERIFY(!apply->isEnabled());
        api->findChild<QPushButton *>("ApiProofreadSend")->click(); QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(service.requests.size(), 1);
        const auto messages = QJsonDocument::fromJson(service.requestBody).object().value("messages").toArray();
        QCOMPARE(messages.last().toObject().value("content").toString(), selected);
        QCOMPARE(QApplication::clipboard()->text(), QString("keep"));
        auto cursor = draft(*dialog)->textCursor(); cursor.clearSelection(); draft(*dialog)->setTextCursor(cursor); QCOMPARE(copied(*dialog), raw);
        preview->selectAll(); preview->insertPlainText(QStringLiteral("<tag> 校对后\n  edited")); apply->click(); QTRY_VERIFY(!api);
        const QString corrected = prefix + QStringLiteral("<tag> 校对后\n  edited") + suffix;
        QCOMPARE(copied(*dialog), corrected); draft(*dialog)->undo(); QCOMPARE(copied(*dialog), raw);
        QVERIFY(!draft(*dialog)->document()->isUndoAvailable()); draft(*dialog)->redo(); QCOMPARE(copied(*dialog), corrected);
        tabs(*dialog)->setCurrentIndex(1); QCOMPARE(copied(*dialog), raw); QVERIFY(!button(*dialog, "OcrApiProofread")->isEnabled());
    }
    void apiFailureCancellationAndClosePreserveTheDraft() {
        ApiTestServer service; service.hold = true; QString error; QVERIFY(saveApiProfile({service.endpoint(), "model"}, &error));
        const QString raw = "untouched\r\ntext"; auto dialog = std::make_unique<OcrResultDialog>(raw); dialog->show();
        button(*dialog, "OcrApiProofread")->click(); auto *api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api);
        auto *send = api->findChild<QPushButton *>("ApiProofreadSend"), *apply = api->findChild<QPushButton *>("ApiProofreadApply");
        send->click(); QTRY_COMPARE(service.requests.size(), 1); QVERIFY(!send->isEnabled()); QVERIFY(!apply->isEnabled());
        api->findChild<QPushButton *>("ApiProofreadCancel")->click(); QTRY_VERIFY(send->isEnabled()); QVERIFY(!apply->isEnabled());
        service.hold = false; service.status = 401; service.body = "private server details";
        send->click(); QTRY_COMPARE(service.requests.size(), 2); QTRY_VERIFY(send->isEnabled()); QVERIFY(!apply->isEnabled());
        QVERIFY(!api->findChild<QLabel *>("ApiProofreadStatus")->text().contains("private"));
        QCOMPARE(copied(*dialog), raw); QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
        service.hold = true; send->click(); QTRY_COMPARE(service.requests.size(), 3);
        delete dialog.release(); QCoreApplication::processEvents();
        QTRY_VERIFY(service.peers.last().isNull() || service.peers.last()->state() == QAbstractSocket::UnconnectedState);
    }
    void apiMissingProfileDoesNotSendOrChangeText() {
        auto dialog = std::make_unique<OcrResultDialog>("original"); button(*dialog, "OcrApiProofread")->click();
        auto *api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api);
        QVERIFY(!api->findChild<QPushButton *>("ApiProofreadSend")->isEnabled());
        QVERIFY(!api->findChild<QPushButton *>("ApiProofreadApply")->isEnabled());
        api->reject(); QCOMPARE(copied(*dialog), QString("original")); QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
    }
    void apiUneditedResponseKeepsExactText() {
        ApiTestServer service; QVERIFY(service.server.isListening()); QString error; QVERIFY(saveApiProfile({service.endpoint(), "model"}, &error));
        const QString returned = QStringLiteral("fixed\u00a0A\r\nB\u2028C\u2029D");
        service.body = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
            {"message", QJsonObject{{"role", "assistant"}, {"content", returned}}}}}}}).toJson();
        auto dialog = std::make_unique<OcrResultDialog>("original"); button(*dialog, "OcrApiProofread")->click();
        auto *api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api); api->findChild<QPushButton *>("ApiProofreadSend")->click();
        auto *apply = api->findChild<QPushButton *>("ApiProofreadApply"); QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(api->correctedText(), returned); apply->click(); QCOMPARE(copied(*dialog), returned);
        draft(*dialog)->undo(); QCOMPARE(copied(*dialog), QString("original")); draft(*dialog)->redo(); QCOMPARE(copied(*dialog), returned);
    }
    void apiUnchangedResponseDoesNotPromiseAnUndo() {
        ApiTestServer service; QVERIFY(service.server.isListening()); QString error; QVERIFY(saveApiProfile({service.endpoint(), "model"}, &error));
        auto dialog = std::make_unique<OcrResultDialog>("corrected"); button(*dialog, "OcrApiProofread")->click();
        auto *api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api); api->findChild<QPushButton *>("ApiProofreadSend")->click();
        auto *apply = api->findChild<QPushButton *>("ApiProofreadApply"); QTRY_VERIFY(apply->isEnabled()); apply->click();
        QVERIFY(!draft(*dialog)->document()->isUndoAvailable());
        QCOMPARE(dialog->findChild<QLabel *>("OcrResultStatus")->text(), t("校对结果与原稿相同，无需更改。"));
    }
    void apiCompletionReturnsKeyboardToTheDraft_data() {
        QTest::addColumn<bool>("commit"); QTest::newRow("apply") << true; QTest::newRow("close") << false;
    }
    void apiCompletionReturnsKeyboardToTheDraft() {
        QFETCH(bool, commit);
        ApiTestServer service; QVERIFY(service.server.isListening()); QString error; QVERIFY(saveApiProfile({service.endpoint(), "model"}, &error));
        QWidget host; host.show(); host.activateWindow();
        auto dialog = std::make_unique<OcrResultDialog>("original", &host); dialog->show(); dialog->activateWindow(); draft(*dialog)->setFocus();
        QTRY_VERIFY(draft(*dialog)->hasFocus());
        button(*dialog, "OcrApiProofread")->click(); auto *api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api);
        if (commit) {
            api->findChild<QPushButton *>("ApiProofreadSend")->click();
            auto *apply = api->findChild<QPushButton *>("ApiProofreadApply"); QTRY_VERIFY(apply->isEnabled()); apply->click();
        } else api->findChild<QPushButton *>("ApiProofreadClose")->click();
        QTRY_COMPARE(QApplication::activeWindow(), dialog.get()); QTRY_COMPARE(QApplication::focusWidget(), draft(*dialog));
        if (commit) QTest::keySequence(QApplication::focusWidget(), QKeySequence::Undo);
        else { QVERIFY(service.requests.isEmpty()); QVERIFY(!draft(*dialog)->document()->isUndoAvailable()); }
        QCOMPARE(copied(*dialog), QString("original"));
    }
    void apiSnapshots_data() {
        QTest::addColumn<QString>("language"); QTest::addColumn<QString>("theme"); QTest::addColumn<QSize>("size");
        QTest::newRow("normal-light-zh") << QString("zh_CN") << QString("light") << QSize(700, 600);
        QTest::newRow("normal-dark-en") << QString("en") << QString("dark") << QSize(700, 600);
        QTest::newRow("compact-dark-zh") << QString("zh_CN") << QString("dark") << QSize(480, 460);
        QTest::newRow("compact-light-en") << QString("en") << QString("light") << QSize(480, 460);
    }
    void apiSnapshots() {
        QFETCH(QString, language); QFETCH(QString, theme); QFETCH(QSize, size);
        InterfacePreferences::instance().apply({{"language", language}, {"theme", theme}});
        ApiTestServer service; QString error; QVERIFY(saveApiProfile({service.endpoint(), "configured-model"}, &error));
        const QString raw = QStringLiteral("会议记录 / Meeting notes\r\n识别文宇需要校对。\r\nKeep  spacing and <tags>.\r\n文字仅在点击发送后提交。");
        auto dialog = std::make_unique<OcrResultDialog>(raw); dialog->resize(size.width() > 480 ? QSize(680, 520) : QSize(480, 420)); dialog->show();
        const QString directory = QDir(QCoreApplication::applicationDirPath()).filePath("../.impeccable/review/offscreen"); QVERIFY(QDir().mkpath(directory));
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        QTest::qWait(20); QVERIFY(dialog->grab().save(directory + "/ocr-api-entry-" + tag + ".png"));
        button(*dialog, "OcrApiProofread")->click(); auto *api = dialog->findChild<ApiProofreadDialog *>(); QVERIFY(api); api->resize(size);
        QTest::qWait(20); QVERIFY(api->grab().save(directory + "/ocr-api-before-send-" + tag + ".png"));
        auto json = QJsonDocument::fromJson(service.body).object(); auto choices = json["choices"].toArray(); auto choice = choices[0].toObject();
        choice["message"] = QJsonObject{{"role", "assistant"}, {"content", QStringLiteral("会议记录 / Meeting notes\n识别文字需要校对。\nKeep  spacing and <tags>.\n文字仅在点击发送后提交。")}};
        choices[0] = choice; json["choices"] = choices; service.body = QJsonDocument(json).toJson();
        api->findChild<QPushButton *>("ApiProofreadSend")->click(); QTRY_VERIFY(api->findChild<QPushButton *>("ApiProofreadApply")->isEnabled());
        auto *preview = api->findChild<QPlainTextEdit *>("ApiProofreadPreview"); preview->setFocus();
        QTest::qWait(20); QVERIFY(api->grab().save(directory + "/ocr-api-preview-" + tag + ".png"));
        api->reject();
    }
    void typingEnterEditsTextWithoutClosingOrCopying() {
        auto dialog = std::make_unique<OcrResultDialog>(QString("A"));
        QSignalSpy finished(dialog.get(), &QDialog::finished);
        dialog->show(); dialog->activateWindow(); QTRY_VERIFY(dialog->isActiveWindow());
        draft(*dialog)->setFocus(); QTRY_VERIFY(draft(*dialog)->hasFocus()); draft(*dialog)->moveCursor(QTextCursor::End);
        QApplication::clipboard()->setText("keep");
        QTest::keyClick(draft(*dialog), Qt::Key_Return); QTest::keyClicks(draft(*dialog), "B");
        QCOMPARE(draft(*dialog)->toPlainText(), QString("A\nB")); QCOMPARE(finished.count(), 0);
        QCOMPARE(QApplication::clipboard()->text(), QString("keep"));
    }
    void closingOnlyDisposesTheSnapshot_data() {
        QTest::addColumn<QString>("method");
        QTest::newRow("button") << QString("button"); QTest::newRow("escape") << QString("escape"); QTest::newRow("window") << QString("window");
    }
    void closingOnlyDisposesTheSnapshot() {
        QFETCH(QString, method);
        QPointer<OcrResultDialog> dialog = new OcrResultDialog("original"); dialog->show();
        draft(*dialog)->insertPlainText("edited"); QApplication::clipboard()->setText("keep");
        QSignalSpy finished(dialog, &QDialog::finished);
        if (method == "button") button(*dialog, "OcrCloseResult")->click();
        else if (method == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
        else dialog->close();
        QCOMPARE(finished.count(), 1); QTRY_VERIFY(!dialog); QCOMPARE(QApplication::clipboard()->text(), QString("keep"));
    }
};

QTEST_MAIN(OcrResultDialogTest)
#include "ocr_result_dialog_test.moc"
