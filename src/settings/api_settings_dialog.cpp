#include "api_settings_dialog.h"
#include "api_profile.h"
#include "imaging/api_proofreader.h"
#include "platform/api_credentials.h"
#include "ui/i18n.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>

namespace still {

ApiSettingsDialog::ApiSettingsDialog(QWidget *parent)
    : QDialog(parent), client_(new ApiProofreader(this)), intro_(new QLabel(this)),
      endpointLabel_(new QLabel(this)), modelLabel_(new QLabel(this)), keyLabel_(new QLabel(this)),
      credential_(new QLabel(this)), status_(new QLabel(this)), endpoint_(new QLineEdit(this)),
      model_(new QLineEdit(this)), key_(new QLineEdit(this)), remove_(new QPushButton(this)),
      test_(new QPushButton(this)), save_(new QPushButton(this)), close_(new QPushButton(this)) {
    setObjectName("ApiSettingsDialog"); setWindowModality(Qt::WindowModal); setAttribute(Qt::WA_DeleteOnClose);
    resize(620, 370); setMinimumWidth(480);
    for (auto *label : {intro_, credential_, status_}) { label->setTextFormat(Qt::PlainText); label->setWordWrap(true); }
    endpoint_->setObjectName("ApiEndpoint"); model_->setObjectName("ApiModel"); key_->setObjectName("ApiKey");
    credential_->setObjectName("ApiCredentialStatus"); status_->setObjectName("ApiSettingsStatus");
    remove_->setObjectName("ApiRemoveKey"); test_->setObjectName("ApiTestConnection");
    save_->setObjectName("ApiSaveSettings"); close_->setObjectName("ApiCloseSettings");
    key_->setEchoMode(QLineEdit::Password); key_->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText | Qt::ImhSensitiveData);
    for (auto *button : {remove_, test_, save_, close_}) button->setAutoDefault(false);
    auto *layout = new QVBoxLayout(this); layout->setSpacing(12); layout->addWidget(intro_);
    auto *form = new QFormLayout;
    form->addRow(endpointLabel_, endpoint_); form->addRow(modelLabel_, model_); form->addRow(keyLabel_, key_);
    endpointLabel_->setBuddy(endpoint_); modelLabel_->setBuddy(model_); keyLabel_->setBuddy(key_);
    layout->addLayout(form);
    auto *credentials = new QHBoxLayout; credentials->addWidget(credential_, 1); credentials->addWidget(remove_); layout->addLayout(credentials);
    layout->addWidget(status_); layout->addStretch();
    auto *buttons = new QHBoxLayout; buttons->addWidget(test_); buttons->addStretch(); buttons->addWidget(save_); buttons->addWidget(close_); layout->addLayout(buttons);
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this); setStyle(style);
    for (auto *widget : findChildren<QWidget *>()) widget->setStyle(style);
    if (const auto saved = loadApiProfile(&detail_)) {
        endpoint_->setText(saved->endpoint.toString(QUrl::FullyEncoded)); model_->setText(saved->model);
    }
    failed_ = !detail_.isEmpty();
    refreshCredential();
    connect(endpoint_, &QLineEdit::textChanged, this, [this] {
        key_->clear(); hasCredential_ = false; credential_->setText(t("测试或保存时检查此地址的密钥。")); remove_->setEnabled(false);
    });
    connect(endpoint_, &QLineEdit::editingFinished, this, &ApiSettingsDialog::refreshCredential);
    connect(test_, &QPushButton::clicked, this, &ApiSettingsDialog::testConnection);
    connect(save_, &QPushButton::clicked, this, &ApiSettingsDialog::save);
    connect(remove_, &QPushButton::clicked, this, &ApiSettingsDialog::removeKey);
    connect(close_, &QPushButton::clicked, this, &ApiSettingsDialog::reject);
    connect(client_, &ApiProofreader::finished, this, [this](const ApiProofreadResult &result) {
        failed_ = !result.error.isEmpty();
        if (result.canceled) detail_ = t("已取消测试；服务端可能已产生用量。");
        else if (failed_) detail_ = result.error;
        else {
            detail_ = t("连接测试成功。仅发送了固定测试文字。");
            detail_ += '\n' + (result.inputTokens >= 0 || result.outputTokens >= 0 || result.totalTokens >= 0
                ? t("用量：输入 %1，输出 %2，合计 %3 tokens。")
                    .arg(result.inputTokens >= 0 ? QString::number(result.inputTokens) : t("未知"))
                    .arg(result.outputTokens >= 0 ? QString::number(result.outputTokens) : t("未知"))
                    .arg(result.totalTokens >= 0 ? QString::number(result.totalTokens) : t("未知"))
                : t("服务未返回用量。"));
        }
        refreshCredential(); updateText();
    });
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, [this] { refreshCredential(); updateText(); });
    updateText();
}

ApiProfile ApiSettingsDialog::profile() const { return {QUrl(endpoint_->text().trimmed(), QUrl::StrictMode), model_->text().trimmed()}; }

QByteArray ApiSettingsDialog::requestKey(QString *error) const {
    if (!key_->text().isEmpty()) return key_->text().toUtf8();
    const auto saved = readApiCredential(apiCredentialAccount(profile().endpoint));
    *error = saved.error; return saved.key;
}

void ApiSettingsDialog::updateText() {
    setWindowTitle(t("API 文字校对设置"));
    intro_->setText(t("系统 OCR 和本机整理无需密钥。API 校对使用你配置的服务，仅在明确发送后上传文字；结果先预览再应用。"));
    endpointLabel_->setText(t("完整服务地址：")); modelLabel_->setText(t("模型：")); keyLabel_->setText(t("API Key："));
    endpoint_->setPlaceholderText("https://api.example.com/v1/chat/completions"); model_->setPlaceholderText(t("填写服务支持的模型名称"));
    key_->setPlaceholderText(t("留空保留此地址已保存的密钥"));
    endpoint_->setAccessibleName(t("API 完整服务地址")); model_->setAccessibleName(t("API 模型")); key_->setAccessibleName(t("API 密钥"));
    remove_->setText(t("删除此地址密钥")); test_->setText(client_->busy() ? t("取消测试") : t("测试连接"));
    save_->setText(t("保存")); close_->setText(t("关闭"));
    const bool busy = client_->busy();
    for (auto *field : {endpoint_, model_, key_}) field->setEnabled(!busy);
    save_->setEnabled(!busy); remove_->setEnabled(!busy && hasCredential_);
    status_->setText(busy ? t("正在发送固定测试文字，请稍候…") : InterfacePreferences::instance().translatedCaption(detail_));
    auto colors = status_->palette(); colors.setColor(QPalette::WindowText,
        failed_ ? QColor(InterfacePreferences::instance().dark() ? "#ff9b90" : "#b42318") : palette().color(QPalette::WindowText)); status_->setPalette(colors);
    for (auto *button : {remove_, test_, save_, close_}) button->setAccessibleName(button->text());
}

void ApiSettingsDialog::refreshCredential() {
    const auto current = profile();
    if (!ApiProofreader::endpointError(current.endpoint).isEmpty()) {
        credential_->setText(t("密钥保存在系统凭据存储，不进入配置备份。")); hasCredential_ = false; remove_->setEnabled(false); return;
    }
    const auto saved = readApiCredential(apiCredentialAccount(current.endpoint));
    hasCredential_ = !saved.key.isEmpty();
    credential_->setText(!saved.error.isEmpty() ? InterfacePreferences::instance().translatedCaption(saved.error)
        : hasCredential_ ? t("此地址已保存密钥。密钥不进入配置备份。") : t("此地址未保存密钥。本机 HTTP 服务可不填密钥。"));
    remove_->setEnabled(!client_->busy() && hasCredential_);
}

void ApiSettingsDialog::testConnection() {
    if (client_->busy()) { client_->cancel(); return; }
    const auto current = profile(); QString error;
    const auto addressError = ApiProofreader::endpointError(current.endpoint);
    if (!addressError.isEmpty()) error = addressError;
    QByteArray key;
    if (error.isEmpty()) key = requestKey(&error);
    if (!error.isEmpty()) { detail_ = error; failed_ = true; updateText(); return; }
    detail_.clear(); failed_ = false;
    client_->start(current.endpoint, current.model, key, QStringLiteral("Still API connection check.")); updateText();
}

void ApiSettingsDialog::save() {
    const auto current = profile(); QString error;
    const auto addressError = ApiProofreader::endpointError(current.endpoint);
    if (!addressError.isEmpty()) error = addressError;
    QByteArray key;
    if (error.isEmpty()) key = requestKey(&error);
    if (error.isEmpty()) error = ApiProofreader::validationError(current.endpoint, current.model, key, QStringLiteral("check"));
    if (!error.isEmpty()) { detail_ = error; failed_ = true; updateText(); return; }
    const bool changedKey = !key_->text().isEmpty();
    if (changedKey && !writeApiCredential(apiCredentialAccount(current.endpoint), key, &error)) {
        detail_ = error; failed_ = true; updateText(); return;
    }
    if (!saveApiProfile(current, &error)) {
        detail_ = changedKey ? t("密钥已保存，但服务设置未保存：%1").arg(error) : error;
        failed_ = true; refreshCredential(); updateText(); return;
    }
    key_->clear(); accept();
}

void ApiSettingsDialog::removeKey() {
    const auto current = profile(); QString error = ApiProofreader::endpointError(current.endpoint);
    if (error.isEmpty() && removeApiCredential(apiCredentialAccount(current.endpoint), &error)) {
        key_->clear(); detail_ = t("已删除此地址密钥，服务地址和模型保持不变。"); failed_ = false;
    } else { detail_ = error; failed_ = true; }
    refreshCredential(); updateText();
}

void ApiSettingsDialog::reject() { client_->cancel(); QDialog::reject(); }

} // namespace still
