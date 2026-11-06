#include "capture_presets.h"
#include "ui/i18n.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace still {

CapturePresets::CapturePresets(QString path, QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint), path_(std::move(path)),
      list_(new QListWidget(this)), nameLabel_(new QLabel(this)), status_(new QLabel(this)),
      name_(new QLineEdit(this)), save_(new QPushButton(this)), capture_(new QPushButton(this)),
      remove_(new QPushButton(this)), close_(new QPushButton(this)) {
    setObjectName("RegionPresetsDialog");
    setWindowModality(Qt::NonModal); setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    setMinimumWidth(390);
    list_->setObjectName("RegionPresetList"); list_->setMinimumHeight(160);
    name_->setObjectName("RegionPresetName"); name_->setMaxLength(80); name_->setClearButtonEnabled(true);
    nameLabel_->setBuddy(name_);
    status_->setObjectName("RegionPresetStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    save_->setObjectName("SaveRegionPreset"); save_->setDefault(true);
    capture_->setObjectName("CaptureRegionPreset");
    remove_->setObjectName("DeleteRegionPreset");
    close_->setObjectName("CloseRegionPresets");
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(list_);
    auto *form = new QFormLayout; form->addRow(nameLabel_, name_); layout->addLayout(form);
    layout->addWidget(save_); layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(capture_); buttons->addWidget(remove_); buttons->addStretch(); buttons->addWidget(close_);
    layout->addLayout(buttons);
    // Native Cocoa buttons do not honor this dialog's dark palette.
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this);
    setStyle(style);
    for (auto *widget : findChildren<QWidget *>()) widget->setStyle(style);
    const auto loaded = loadRegionPresets(path_, &error_);
    readable_ = loaded.has_value();
    fileError_ = !readable_;
    if (loaded) {
        presets_ = *loaded;
        for (const auto &preset : presets_) list_->addItem(preset.name);
        if (!presets_.isEmpty()) list_->setCurrentRow(0);
    }
    list_->setEnabled(readable_); name_->setEnabled(readable_);
    connect(name_, &QLineEdit::textChanged, this, &CapturePresets::updateActions);
    connect(list_, &QListWidget::currentRowChanged, this, &CapturePresets::updateActions);
    connect(list_, &QListWidget::itemActivated, this, &CapturePresets::captureSelected);
    connect(save_, &QPushButton::clicked, this, &CapturePresets::saveCurrentRegion);
    connect(capture_, &QPushButton::clicked, this, &CapturePresets::captureSelected);
    // Mac item views use Return for editing, so scope capture to this read-only list.
    auto *captureKeys = new QShortcut(list_);
    captureKeys->setKeys({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    captureKeys->setContext(Qt::WidgetWithChildrenShortcut);
    connect(captureKeys, &QShortcut::activated, capture_, &QPushButton::click);
    connect(remove_, &QPushButton::clicked, this, &CapturePresets::deleteSelected);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, &CapturePresets::updateText);
    updateText(); updateActions();
}

void CapturePresets::setCurrentRegion(const RegionPreset &last) {
    last_ = last; updateText(); updateActions();
}

void CapturePresets::updateText() {
    setWindowTitle(t("区域预设"));
    list_->setAccessibleName(t("区域预设列表"));
    nameLabel_->setText(t("预设名称：")); name_->setAccessibleName(t("预设名称")); name_->setPlaceholderText(t("输入预设名称"));
    save_->setText(t("保存上次区域")); capture_->setText(t("截取所选")); remove_->setText(t("删除所选")); close_->setText(t("关闭"));
    for (auto *button : {save_, capture_, remove_, close_}) button->setAccessibleName(button->text());
    if (!error_.isEmpty()) {
        const QString reason = InterfacePreferences::instance().translatedCaption(error_);
        status_->setText(fileError_ ? reason + '\n' + path_ : reason);
    } else if (last_.region.isEmpty()) status_->setText(t("先完成一次区域截图，即可保存为预设。"));
    else if (presets_.isEmpty()) status_->setText(t("输入名称，保存上次确认的区域。"));
    else status_->setText(t("选择预设后可直接截图。"));
}

void CapturePresets::updateActions() {
    save_->setEnabled(readable_ && !last_.region.isEmpty() && !name_->text().trimmed().isEmpty());
    const bool selected = readable_ && list_->currentRow() >= 0;
    capture_->setEnabled(selected); remove_->setEnabled(selected);
}

void CapturePresets::saveCurrentRegion() {
    auto preset = last_; preset.name = name_->text().trimmed();
    for (const auto &stored : presets_) {
        if (stored.name == preset.name) { error_ = t("名称已存在，请使用其他名称。"); fileError_ = false; updateText(); return; }
    }
    auto updated = presets_; updated.append(preset);
    if (!saveRegionPresets(path_, updated, &error_)) { fileError_ = true; updateText(); return; }
    presets_ = std::move(updated); error_.clear(); fileError_ = false;
    list_->addItem(preset.name); list_->setCurrentRow(list_->count() - 1);
    name_->clear(); updateText();
}

void CapturePresets::captureSelected() {
    const auto preset = presets_[list_->currentRow()];
    hide(); emit captureRequested(preset);
}

void CapturePresets::deleteSelected() {
    const int row = list_->currentRow();
    QMessageBox confirmation(QMessageBox::Question, t("删除区域预设"), t("删除预设“%1”？").arg(presets_[row].name),
                             QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setObjectName("DeleteRegionPresetConfirmation");
    confirmation.setTextFormat(Qt::PlainText); confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setStyle(style());
    for (auto *widget : confirmation.findChildren<QWidget *>()) widget->setStyle(style());
    if (confirmation.exec() != QMessageBox::Yes) return;
    auto updated = presets_; updated.removeAt(row);
    if (!saveRegionPresets(path_, updated, &error_)) { fileError_ = true; updateText(); return; }
    presets_ = std::move(updated); error_.clear(); fileError_ = false;
    delete list_->takeItem(row);
    if (!presets_.isEmpty()) list_->setCurrentRow(qMin(row, int(presets_.size()) - 1));
    updateText(); updateActions();
}

} // namespace still
