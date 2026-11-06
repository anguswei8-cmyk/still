#pragma once

#include <QByteArray>
#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace still {
class ApiProofreader;
struct ApiProfile;

class ApiSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApiSettingsDialog(QWidget *parent = nullptr);
    void reject() override;
private:
    ApiProofreader *client_;
    QLabel *intro_, *endpointLabel_, *modelLabel_, *keyLabel_, *credential_, *status_;
    QLineEdit *endpoint_, *model_, *key_;
    QPushButton *remove_, *test_, *save_, *close_;
    QString detail_;
    bool failed_ = false;
    bool hasCredential_ = false;
    void updateText();
    void refreshCredential();
    ApiProfile profile() const;
    QByteArray requestKey(QString *error) const;
    void testConnection();
    void save();
    void removeKey();
};

} // namespace still
