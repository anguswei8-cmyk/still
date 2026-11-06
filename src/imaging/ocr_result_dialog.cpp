#include "ocr_result_dialog.h"
#include "api_proofread_dialog.h"
#include "ui/i18n.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace still {
namespace {

class ResultTextEdit : public QPlainTextEdit {
public:
    ResultTextEdit(const QString &text, QWidget *parent)
        : QPlainTextEdit(parent) {
        setPlainText(text); rawStates_.insert(0, text);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        connect(document(), &QTextDocument::undoCommandAdded, this, [this] {
            rawStates_.erase(rawStates_.lowerBound(document()->availableUndoSteps()), rawStates_.end());
        });
        connect(document(), &QTextDocument::contentsChanged, this, [this] {
            const int step = document()->availableUndoSteps();
            // Consecutive typing can merge into the same Qt undo command.
            if (step == undoStep_) rawStates_.remove(step);
            undoStep_ = step;
        });
    }
    QString fullText() const {
        const auto state = rawStates_.constFind(document()->availableUndoSteps());
        // Programmatic edits retain exact separators; manual edits use Qt's document text.
        return state != rawStates_.cend() ? *state : document()->toRawText().replace(QChar::ParagraphSeparator, '\n');
    }
    qsizetype rawPosition(int documentPosition) const {
        const auto text = fullText();
        qsizetype raw = 0;
        // QTextDocument represents a CRLF pair with one paragraph separator.
        for (int position = 0; position < documentPosition; ++position, ++raw)
            if (text[raw] == '\r' && raw + 1 < text.size() && text[raw + 1] == '\n') ++raw;
        return raw;
    }
    void replaceText(const QString &text) {
        if (fullText() == text) return;
        auto cursor = textCursor(); cursor.beginEditBlock(); cursor.select(QTextCursor::Document); cursor.insertText(text); cursor.endEditBlock();
        rawStates_.insert(document()->availableUndoSteps(), text); setTextCursor(cursor);
    }
protected:
    QMimeData *createMimeDataFromSelection() const override {
        auto *data = new QMimeData;
        const auto cursor = textCursor();
        data->setText(cursor.selectionStart() == 0 && cursor.selectionEnd() == document()->characterCount() - 1
                          ? fullText() : cursor.selectedText().replace(QChar::ParagraphSeparator, '\n'));
        return data;
    }
private:
    QMap<int, QString> rawStates_;
    int undoStep_ = 0;
};

QString fullText(const QPlainTextEdit *editor) { return static_cast<const ResultTextEdit *>(editor)->fullText(); }

bool lineBreak(QChar character) {
    return character == '\r' || character == '\n' || character == QChar::LineSeparator
        || character == QChar::ParagraphSeparator || character == QChar(0x85);
}

QString formatText(const QString &text, int lineBreaks, int spaces) {
    QString lines; lines.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar character = text[i];
        if (!lineBreaks || !lineBreak(character)) lines += character;
        else {
            if (lineBreaks == 1) lines += ' ';
            if (character == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        }
    }
    if (!spaces) return lines;
    QString result; result.reserve(lines.size());
    bool previousSpace = false;
    for (QChar character : lines) {
        const bool horizontal = character.isSpace() && !lineBreak(character);
        if (!horizontal) result += character;
        else if (spaces == 1 && !previousSpace) result += ' ';
        previousSpace = horizontal;
    }
    return result;
}

} // namespace

OcrResultDialog::OcrResultDialog(QString original, QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint), originalText_(std::move(original)),
      tabs_(new QTabWidget(this)), edited_(new ResultTextEdit(originalText_, tabs_)),
      original_(new ResultTextEdit(originalText_, tabs_)), hint_(new QLabel(this)),
      lineBreaksLabel_(new QLabel(this)), spacesLabel_(new QLabel(this)), status_(new QLabel(this)),
      lineBreaks_(new QComboBox(this)), spaces_(new QComboBox(this)), apply_(new QPushButton(this)), api_(new QPushButton(this)),
      restore_(new QPushButton(this)), copy_(new QPushButton(this)), close_(new QPushButton(this)) {
    setObjectName("OcrResultDialog"); setWindowModality(Qt::NonModal); setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    resize(680, 520); setMinimumSize(480, 360);
    tabs_->setObjectName("OcrResultTabs");
    edited_->setObjectName("OcrEditedText"); original_->setObjectName("OcrOriginalText");
    original_->setReadOnly(true);
    tabs_->addTab(edited_, QString()); tabs_->addTab(original_, QString());
    hint_->setTextFormat(Qt::PlainText); hint_->setWordWrap(true);
    status_->setObjectName("OcrResultStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    lineBreaks_->setObjectName("OcrLineBreaks"); spaces_->setObjectName("OcrSpaces");
    for (auto *combo : {lineBreaks_, spaces_}) { combo->addItems({QString(), QString(), QString()}); combo->setSizeAdjustPolicy(QComboBox::AdjustToContents); }
    lineBreaksLabel_->setBuddy(lineBreaks_); spacesLabel_->setBuddy(spaces_);
    apply_->setObjectName("OcrApplyFormatting"); restore_->setObjectName("OcrRestoreOriginal");
    api_->setObjectName("OcrApiProofread");
    copy_->setObjectName("OcrCopyText"); close_->setObjectName("OcrCloseResult");
    for (auto *button : {apply_, api_, restore_, copy_, close_}) button->setAutoDefault(false);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint_); layout->addWidget(tabs_, 1);
    auto *format = new QGridLayout;
    format->addWidget(lineBreaksLabel_, 0, 0); format->addWidget(lineBreaks_, 0, 1);
    format->addWidget(spacesLabel_, 1, 0); format->addWidget(spaces_, 1, 1);
    format->addWidget(apply_, 0, 2, 2, 1); format->setColumnStretch(1, 1);
    layout->addLayout(format);
    auto *apiActions = new QHBoxLayout; apiActions->addWidget(api_); apiActions->addStretch(); layout->addLayout(apiActions);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(restore_); buttons->addStretch(); buttons->addWidget(copy_); buttons->addWidget(close_);
    layout->addLayout(buttons);
    // Native Cocoa buttons do not honor this dialog's dark palette.
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this); setStyle(style);
    for (auto *widget : findChildren<QWidget *>()) widget->setStyle(style);
    connect(apply_, &QPushButton::clicked, this, &OcrResultDialog::applyFormatting);
    connect(api_, &QPushButton::clicked, this, &OcrResultDialog::proofreadWithApi);
    connect(restore_, &QPushButton::clicked, this, [this] {
        replaceEditedText(originalText_); tabs_->setCurrentIndex(0); state_ = Status::Restored; updateText();
    });
    connect(copy_, &QPushButton::clicked, this, &OcrResultDialog::copyText);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    connect(tabs_, &QTabWidget::currentChanged, this, &OcrResultDialog::updateActions);
    for (auto *combo : {lineBreaks_, spaces_}) connect(combo, &QComboBox::currentIndexChanged, this, &OcrResultDialog::updateActions);
    for (auto *editor : {edited_, original_}) connect(editor, &QPlainTextEdit::selectionChanged, this, &OcrResultDialog::updateActions);
    connect(edited_, &QPlainTextEdit::textChanged, this, [this] { state_ = Status::None; updateText(); });
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, &OcrResultDialog::updateText);
    updateText();
}

void OcrResultDialog::updateText() {
    setWindowTitle(t("识别结果"));
    hint_->setText(t("识别结果快照；校对不改变图片。选中文字可拖出，移动后可撤销。"));
    tabs_->setTabText(0, t("校对")); tabs_->setTabText(1, t("原始结果"));
    edited_->setAccessibleName(t("校对文字")); original_->setAccessibleName(t("原始识别文字"));
    edited_->setPlaceholderText(t("未识别到文字")); original_->setPlaceholderText(t("未识别到文字"));
    lineBreaksLabel_->setText(t("换行：")); spacesLabel_->setText(t("空格："));
    lineBreaks_->setAccessibleName(t("换行处理")); spaces_->setAccessibleName(t("空格处理"));
    lineBreaks_->setItemText(0, t("保留换行")); lineBreaks_->setItemText(1, t("换行转为空格")); lineBreaks_->setItemText(2, t("移除换行"));
    spaces_->setItemText(0, t("保留空格")); spaces_->setItemText(1, t("合并连续空格")); spaces_->setItemText(2, t("移除空格"));
    apply_->setText(t("应用整理")); restore_->setText(t("还原原文")); close_->setText(t("关闭"));
    status_->setText(state_ == Status::Formatted ? t("已整理，可撤销。") : state_ == Status::Restored ? t("已还原原文，可撤销。")
                     : state_ == Status::Copied ? t("已复制文字") : state_ == Status::ApiApplied ? t("已应用 API 校对，可撤销。")
                     : state_ == Status::ApiUnchanged ? t("校对结果与原稿相同，无需更改。") : QString());
    updateActions();
}

void OcrResultDialog::updateActions() {
    const bool editing = tabs_->currentIndex() == 0;
    const auto *current = editing ? edited_ : original_;
    apply_->setEnabled(editing && !edited_->toPlainText().isEmpty() && (lineBreaks_->currentIndex() || spaces_->currentIndex()));
    lineBreaks_->setEnabled(editing); spaces_->setEnabled(editing);
    api_->setText(edited_->textCursor().hasSelection() ? t("API 校对选中文字…") : t("API 校对全部文字…"));
    api_->setEnabled(editing && !edited_->toPlainText().isEmpty());
    restore_->setEnabled(fullText(edited_) != originalText_);
    copy_->setText(current->textCursor().hasSelection() ? t("复制选中文字") : t("复制全部文字"));
    copy_->setEnabled(!current->toPlainText().isEmpty());
    for (auto *button : {apply_, api_, restore_, copy_, close_}) button->setAccessibleName(button->text());
}

void OcrResultDialog::replaceEditedText(const QString &text) {
    static_cast<ResultTextEdit *>(edited_)->replaceText(text);
}

void OcrResultDialog::applyFormatting() {
    replaceEditedText(formatText(fullText(edited_), lineBreaks_->currentIndex(), spaces_->currentIndex()));
    state_ = Status::Formatted; updateText(); edited_->setFocus();
}

void OcrResultDialog::proofreadWithApi() {
    auto *editor = static_cast<ResultTextEdit *>(edited_);
    const QString text = editor->fullText(); const auto cursor = editor->textCursor();
    const bool selection = cursor.hasSelection();
    const qsizetype start = selection ? editor->rawPosition(cursor.selectionStart()) : 0;
    const qsizetype end = selection ? editor->rawPosition(cursor.selectionEnd()) : text.size();
    auto *dialog = new ApiProofreadDialog(text.mid(start, end - start), selection, this);
    connect(dialog, &QDialog::accepted, this, [this, dialog, text, start, end] {
        const QString corrected = text.left(start) + dialog->correctedText() + text.mid(end);
        replaceEditedText(corrected);
        state_ = corrected == text ? Status::ApiUnchanged : Status::ApiApplied; updateText();
    });
    connect(dialog, &QDialog::finished, this, [this] {
        // Restore focus after the modal window has completed its close processing.
        QTimer::singleShot(0, this, [this] { activateWindow(); edited_->setFocus(); });
    });
    dialog->open();
}

void OcrResultDialog::copyText() {
    auto *current = tabs_->currentIndex() == 0 ? edited_ : original_;
    if (current->textCursor().hasSelection()) current->copy();
    else QApplication::clipboard()->setText(fullText(current));
    state_ = Status::Copied; updateText();
}

} // namespace still
