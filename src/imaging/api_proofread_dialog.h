#pragma once

#include "api_proofreader.h"
#include "settings/api_profile.h"
#include <QDialog>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace still {

class ApiProofreadDialog : public QDialog {
    Q_OBJECT
public:
    ApiProofreadDialog(QString text, bool selection, QWidget *parent = nullptr);
    QString correctedText() const;
protected:
    void done(int result) override;
private:
    const QString text_;
    const bool selection_;
    std::optional<ApiProfile> profile_;
    ApiProofreader request_;
    ApiProofreadResult result_;
    QString error_;
    bool hasResult_ = false;
    QLabel *intro_, *destination_, *sourceLabel_, *resultLabel_, *status_;
    QPlainTextEdit *source_, *preview_;
    QPushButton *settings_, *send_, *cancel_, *apply_, *close_;
    void reloadProfile();
    void send();
    void updateText();
};

} // namespace still
