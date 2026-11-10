#include "settings_backup_dialog.h"
#include "storage/store.h"
#include "settings_apply.h"
#include "settings_backup.h"
#include "ui/i18n.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>
#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

namespace still {
namespace {
const QStringList directoryGroups{"save", "history", "clipboard"};

void keepDirectories(QJsonObject &candidate, const QJsonObject &current) {
    for (const auto &group : directoryGroups) {
        auto values = candidate.value(group).toObject();
        values["dir"] = current.value(group).toObject().value("dir").toString(); candidate[group] = values;
    }
}

QString displayValue(const QVariantMap &row, const QJsonValue &value) {
    if (value.isBool()) return value.toBool() ? t("开启") : t("关闭");
    const QString type = row.value("type").toString(), text = value.toVariant().toString();
    if (type == "choice") for (const auto &option : row.value("options").toList()) {
        const auto item = option.toMap(); if (item.value("value").toString() == text) return item.value("text").toString();
    }
    if (type == "shortcut") return text.isEmpty() ? t("未设置") : QKeySequence::fromString(text, QKeySequence::PortableText).toString(QKeySequence::NativeText);
    if (type == "directory" && text.isEmpty()) return t("系统默认目录");
    const QString unit = row.value("unit").toString();
    return unit.isEmpty() ? text : text + ' ' + unit;
}

QString protectedSettingsDestinationError(const QFileInfo &destination, const QFileInfo &active) {
    if (QDir::cleanPath(destination.absoluteFilePath()) == QDir::cleanPath(active.absoluteFilePath()) ||
        (!destination.canonicalFilePath().isEmpty() && destination.canonicalFilePath() == active.canonicalFilePath()))
        return t("请选择其他文件，不能覆盖正在使用的配置。");
    const QString parent = QFileInfo(active.absolutePath()).canonicalFilePath();
    if (parent.isEmpty() || parent != QFileInfo(destination.absolutePath()).canonicalFilePath() ||
        destination.fileName().compare(active.fileName(), Qt::CaseInsensitive) != 0) return {};
#ifdef Q_OS_WIN
    return t("请选择其他文件，不能覆盖正在使用的配置。");
#elif defined(Q_OS_MACOS)
    // The settings file may not exist yet; query its existing parent volume instead.
    const long caseSensitive = ::pathconf(QFile::encodeName(parent).constData(), _PC_CASE_SENSITIVE);
    if (caseSensitive == -1) return t("无法确认目标目录的大小写规则，请选择其他文件，原设置已保留。");
    if (caseSensitive == 0) return t("请选择其他文件，不能覆盖正在使用的配置。");
#endif
    return {};
}
} // namespace

SettingsBackupDialog::SettingsBackupDialog(SettingsApply &settings, QWidget *parent)
    : QDialog(parent), settings_(settings), intro_(new QLabel(this)), pathLabel_(new QLabel(this)),
      status_(new QLabel(this)), preview_(new QPlainTextEdit(this)), directories_(new QCheckBox(this)),
      export_(new QPushButton(this)), choose_(new QPushButton(this)), import_(new QPushButton(this)),
      defaults_(new QPushButton(this)), close_(new QPushButton(this)) {
    setObjectName("SettingsBackupDialog"); resize(660, 520); setMinimumSize(480, 400);
    for (auto *label : {intro_, pathLabel_, status_}) { label->setTextFormat(Qt::PlainText); label->setWordWrap(true); }
    pathLabel_->setObjectName("SettingsBackupPath"); status_->setObjectName("SettingsBackupStatus");
    preview_->setObjectName("SettingsBackupPreview"); preview_->setReadOnly(true);
    directories_->setObjectName("RestoreBackupDirectories");
    export_->setObjectName("ExportSettingsBackup"); choose_->setObjectName("ChooseSettingsBackup");
    import_->setObjectName("ImportSettingsBackup"); defaults_->setObjectName("RestoreDefaultSettings");
    close_->setObjectName("CloseSettingsBackup");
    for (auto *button : {export_, choose_, import_, defaults_, close_}) button->setAutoDefault(false);
    auto *layout = new QVBoxLayout(this); layout->addWidget(intro_);
    auto *files = new QHBoxLayout; files->addWidget(export_); files->addWidget(choose_); files->addStretch(); layout->addLayout(files);
    layout->addWidget(pathLabel_); layout->addWidget(preview_, 1); layout->addWidget(directories_); layout->addWidget(status_);
    auto *actions = new QHBoxLayout; actions->addWidget(defaults_); actions->addStretch(); actions->addWidget(import_); actions->addWidget(close_); layout->addLayout(actions);
    // Native Cocoa buttons do not honor this dialog's dark palette.
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this); setStyle(style);
    for (auto *widget : findChildren<QWidget *>()) widget->setStyle(style);
    connect(export_, &QPushButton::clicked, this, &SettingsBackupDialog::exportBackup);
    connect(choose_, &QPushButton::clicked, this, &SettingsBackupDialog::chooseBackup);
    connect(import_, &QPushButton::clicked, this, &SettingsBackupDialog::importBackup);
    connect(defaults_, &QPushButton::clicked, this, &SettingsBackupDialog::restoreDefaults);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    connect(directories_, &QCheckBox::toggled, this, &SettingsBackupDialog::updatePreview);
    connect(&settings_, &SettingsApply::finished, this, [this](bool success, const QJsonObject &, const QStringList &, const QString &message) {
        if (applying_) {
            applying_ = false; detail_ = message; failedPath_.clear();
            state_ = success ? (state_ == State::ApplyingImport ? State::Imported : State::Defaulted) : State::Failed;
            updateText();
            if (success) emit settingsRestored();
        } else updateText();
    });
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, &SettingsBackupDialog::updateText);
    updateText();
}

void SettingsBackupDialog::updateText() {
    setWindowTitle(t("配置备份与恢复"));
    intro_->setText(t("备份已保存的偏好，不包含截图、剪贴板内容或密钥。导入前可检查更改。"));
    export_->setText(t("导出配置…")); choose_->setText(t("选择备份…")); import_->setText(t("导入所选"));
    defaults_->setText(t("恢复全部默认…")); close_->setText(t("关闭"));
    directories_->setText(t("同时恢复目录（会迁移已有数据）"));
    directories_->setStyleSheet(QStringLiteral("QCheckBox::indicator:unchecked:enabled { border: 1px solid %1; border-radius: 2px; background: palette(base); }")
        .arg(InterfacePreferences::instance().dark() ? "#adadad" : "#626875"));
    directories_->setAccessibleName(directories_->text()); preview_->setAccessibleName(t("配置更改预览"));
    pathLabel_->setText(path_.isEmpty() ? t("尚未选择备份文件") : path_);
    QString message;
    if (applying_) message = t("正在应用配置，请稍候…");
    else if (settings_.busy()) message = t("等待当前设置保存完成…");
    else if (state_ == State::Imported) message = t("已导入配置。");
    else if (state_ == State::Defaulted) message = t("已恢复默认设置，本机目录保持不变。");
    else if (state_ == State::Exported) message = t("已导出配置备份。");
    else if (state_ == State::Failed) message = t("操作未完成，原设置已保留。");
    if (!detail_.isEmpty()) message += '\n' + InterfacePreferences::instance().translatedCaption(detail_);
    if (!failedPath_.isEmpty()) message += '\n' + failedPath_;
    status_->setText(message.trimmed());
    auto colors = status_->palette();
    colors.setColor(QPalette::WindowText, state_ == State::Failed ? QColor(InterfacePreferences::instance().dark() ? "#ff9b90" : "#b42318") : palette().color(QPalette::WindowText));
    status_->setPalette(colors);
    updateActions(); updatePreview();
}

void SettingsBackupDialog::updateActions() {
    const bool busy = applying_ || settings_.busy();
    export_->setEnabled(!busy); choose_->setEnabled(!busy); defaults_->setEnabled(!busy);
    import_->setEnabled(!busy && imported_.has_value()); directories_->setEnabled(!busy && imported_.has_value());
    close_->setEnabled(!applying_);
    for (auto *button : {export_, choose_, import_, defaults_, close_}) button->setAccessibleName(button->text());
}

QJsonObject SettingsBackupDialog::importCandidate() const {
    auto candidate = *imported_;
    if (!directories_->isChecked()) keepDirectories(candidate, settings_.settings());
    return candidate;
}

void SettingsBackupDialog::updatePreview() {
    if (!imported_) { preview_->setPlainText(t("选择备份后，可检查将要替换的设置。")); return; }
    const auto current = settingsBackupDocument(settings_.settings()).value("settings").toObject(), candidate = importCandidate();
    QStringList paths{t("备份中的目录：")}, changes{t("将应用的更改：")};
    for (const auto &value : backupSettingRows()) {
        const auto row = value.toMap(); const QString group = row.value("group").toString(), key = row.value("key").toString(), title = row.value("title").toString();
        const auto previous = current.value(group).toObject().value(key), next = candidate.value(group).toObject().value(key);
        if (key == "dir") paths.append(t("%1：%2").arg(title, displayValue(row, imported_->value(group).toObject().value(key))));
        if (previous != next) changes.append(t("%1：%2 → %3").arg(title, displayValue(row, previous), displayValue(row, next)));
    }
    if (changes.size() == 1) changes.append(t("没有需要更改的设置。"));
    paths.append(directories_->isChecked() ? t("将按备份恢复目录。") : t("保留本机目录，不使用备份中的路径。"));
    preview_->setPlainText(paths.join('\n') + "\n\n" + changes.join('\n'));
}

void SettingsBackupDialog::exportBackup() {
    QFileDialog chooser(this, t("导出配置备份")); chooser.setObjectName("SettingsBackupSaveDialog");
    chooser.setAcceptMode(QFileDialog::AcceptSave); chooser.setFileMode(QFileDialog::AnyFile); chooser.setNameFilter(t("Still 配置备份 (*.json)"));
    chooser.setDefaultSuffix("json"); chooser.selectFile("Still-settings.json");
    if (chooser.exec() != QDialog::Accepted) return;
    const QString path = chooser.selectedFiles().first(); const QFileInfo destination(path), active(settings_.filePath());
    const QString protectedError = protectedSettingsDestinationError(destination, active);
    if (!protectedError.isEmpty()) {
        state_ = State::Failed; detail_ = protectedError; failedPath_ = path; updateText(); return;
    }
    QString error;
    const bool saved = saveSettingsBackup(path, settings_.settings(), &error);
    state_ = saved ? State::Exported : State::Failed; detail_ = error; failedPath_ = saved ? QString() : path; updateText();
}

void SettingsBackupDialog::chooseBackup() {
    QFileDialog chooser(this, t("选择配置备份")); chooser.setObjectName("SettingsBackupOpenDialog");
    chooser.setAcceptMode(QFileDialog::AcceptOpen); chooser.setFileMode(QFileDialog::ExistingFile); chooser.setNameFilter(t("Still 配置备份 (*.json)"));
    if (chooser.exec() != QDialog::Accepted) return;
    const QString path = chooser.selectedFiles().first(); QFile file(path); QString error;
    if (!file.open(QIODevice::ReadOnly)) error = file.errorString();
    else if (const auto imported = parseSettingsBackup(file.readAll(), &error)) {
        imported_ = *imported; path_ = path; state_ = State::None; detail_.clear(); failedPath_.clear(); updateText(); return;
    }
    state_ = State::Failed; detail_ = error; failedPath_ = path; updateText();
}

bool SettingsBackupDialog::confirm(bool defaults) {
    QMessageBox confirmation(QMessageBox::Question, defaults ? t("恢复全部默认") : t("导入配置"),
        defaults ? t("恢复所有已支持偏好的默认值？本机目录保持不变。此操作会替换未保存的输入；降低历史或剪贴板配额可能清理旧内容。")
                 : t("应用预览中的配置更改？此操作会替换偏好和未保存的输入；降低历史或剪贴板配额可能清理旧内容。"),
        QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setObjectName("SettingsBackupConfirmation"); confirmation.setTextFormat(Qt::PlainText); confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setStyle(style()); for (auto *widget : confirmation.findChildren<QWidget *>()) widget->setStyle(style());
    return confirmation.exec() == QMessageBox::Yes;
}

void SettingsBackupDialog::importBackup() {
    auto candidate = importCandidate();
    if (directories_->isChecked()) for (const auto &group : directoryGroups) {
        const QString path = candidate.value(group).toObject().value("dir").toString();
        if (!path.isEmpty() && !isAbsoluteDirectoryPath(path)) {
            state_ = State::Failed; detail_ = t("备份目录不适用于本机，请保留当前目录或重新选择路径。"); failedPath_ = path; updateText(); return;
        }
    }
    if (confirm(false)) apply(std::move(candidate), State::ApplyingImport);
}

void SettingsBackupDialog::restoreDefaults() {
    auto candidate = defaultSettings(); keepDirectories(candidate, settings_.settings());
    if (confirm(true)) apply(std::move(candidate), State::ApplyingDefaults);
}

void SettingsBackupDialog::apply(QJsonObject candidate, State state) {
    applying_ = true; state_ = state; detail_.clear(); failedPath_.clear(); updateText();
    QString error;
    if (!settings_.applyBackup(std::move(candidate), &error)) {
        applying_ = false; state_ = State::Failed; detail_ = error; updateText();
    }
}

void SettingsBackupDialog::reject() { if (!applying_) QDialog::reject(); }
void SettingsBackupDialog::closeEvent(QCloseEvent *event) { if (applying_) event->ignore(); else QDialog::closeEvent(event); }
} // namespace still
