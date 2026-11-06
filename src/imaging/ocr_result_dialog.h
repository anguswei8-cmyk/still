#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;

namespace still {

class OcrResultDialog : public QDialog {
    Q_OBJECT
public:
    explicit OcrResultDialog(QString original, QWidget *parent = nullptr);
private:
    const QString originalText_;
    QTabWidget *tabs_;
    QPlainTextEdit *edited_;
    QPlainTextEdit *original_;
    QLabel *hint_;
    QLabel *lineBreaksLabel_;
    QLabel *spacesLabel_;
    QLabel *status_;
    QComboBox *lineBreaks_;
    QComboBox *spaces_;
    QPushButton *apply_;
    QPushButton *api_;
    QPushButton *restore_;
    QPushButton *copy_;
    QPushButton *close_;
    enum class Status { None, Formatted, Restored, Copied, ApiApplied, ApiUnchanged };
    Status state_ = Status::None;
    void updateText();
    void updateActions();
    void replaceEditedText(const QString &text);
    void applyFormatting();
    void proofreadWithApi();
    void copyText();
};

} // namespace still
