#pragma once

#include <QDialog>
#include <QJsonObject>
#include <optional>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace still {
class SettingsApply;

class SettingsBackupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsBackupDialog(SettingsApply &settings, QWidget *parent = nullptr);
    void reject() override;
signals:
    void settingsRestored();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    SettingsApply &settings_;
    std::optional<QJsonObject> imported_;
    QString path_, detail_, failedPath_;
    bool applying_ = false;
    enum class State { None, ApplyingImport, ApplyingDefaults, Imported, Defaulted, Exported, Failed };
    State state_ = State::None;
    QLabel *intro_, *pathLabel_, *status_;
    QPlainTextEdit *preview_;
    QCheckBox *directories_;
    QPushButton *export_, *choose_, *import_, *defaults_, *close_;
    void updateText();
    void updateActions();
    void updatePreview();
    QJsonObject importCandidate() const;
    void exportBackup();
    void chooseBackup();
    void importBackup();
    void restoreDefaults();
    void apply(QJsonObject candidate, State state);
    bool confirm(bool defaults);
};
} // namespace still
