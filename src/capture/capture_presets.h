#pragma once

#include "storage/region_presets.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace still {

class CapturePresets : public QDialog {
    Q_OBJECT
public:
    explicit CapturePresets(QString path, QWidget *parent = nullptr);
    void setCurrentRegion(const RegionPreset &last);
signals:
    void captureRequested(const still::RegionPreset &preset);
private:
    QString path_, error_;
    QVector<RegionPreset> presets_;
    RegionPreset last_;
    bool readable_ = false;
    bool fileError_ = false;
    QListWidget *list_;
    QLabel *nameLabel_, *status_;
    QLineEdit *name_;
    QPushButton *save_, *capture_, *remove_, *close_;
    void updateText();
    void updateActions();
    void saveCurrentRegion();
    void captureSelected();
    void deleteSelected();
};

} // namespace still
