#include "api_proofread_dialog.h"
#include "platform/api_credentials.h"
#include "settings/api_settings_dialog.h"
#include "ui/i18n.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QTextDocument>
#include <QVBoxLayout>

namespace still {

ApiProofreadDialog::ApiProofreadDialog(QString text, bool selection, QWidget *parent)
    : QDialog(parent), text_(std::move(text)), selection_(selection), request_(this),
      intro_(new QLabel(this)), destination_(new QLabel(this)), sourceLabel_(new QLabel(this)),
      resultLabel_(new QLabel(this)), status_(new QLabel(this)), source_(new QPlainTextEdit(this)),
      preview_(new QPlainTextEdit(this)), settings_(new QPushButton(this)), send_(new QPushButton(this)),
      cancel_(new QPushButton(this)), apply_(new QPushButton(this)), close_(new QPushButton(this)) {
    setObjectName("ApiProofreadDialog"); setWindowModality(Qt::WindowModal); setAttribute(Qt::WA_DeleteOnClose);
    resize(700, 600); setMinimumSize(480, 460);
    for (auto *label : {intro_, destination_, sourceLabel_, resultLabel_, status_}) {
        label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    }
    destination_->setObjectName("ApiProofreadDestination"); status_->setObjectName("ApiProofreadStatus");
    source_->setObjectName("ApiProofreadSource"); preview_->setObjectName("ApiProofreadPreview");
    source_->setPlainText(text_); source_->setReadOnly(true); preview_->setReadOnly(true);
    sourceLabel_->setBuddy(source_); resultLabel_->setBuddy(preview_);
    settings_->setObjectName("ApiProofreadSettings"); send_->setObjectName("ApiProofreadSend");
    cancel_->setObjectName("ApiProofreadCancel"); apply_->setObjectName("ApiProofreadApply"); close_->setObjectName("ApiProofreadClose");
    auto *layout = new QVBoxLayout(this); layout->addWidget(intro_); layout->addWidget(destination_);
    layout->addWidget(sourceLabel_); layout->addWidget(source_, 1); layout->addWidget(resultLabel_); layout->addWidget(preview_, 1);
    layout->addWidget(status_);
    auto *actions = new QHBoxLayout;
    actions->addWidget(settings_); actions->addStretch(); actions->addWidget(send_); actions->addWidget(cancel_); layout->addLayout(actions);
    auto *finish = new QHBoxLayout; finish->addStretch(); finish->addWidget(apply_); finish->addWidget(close_); layout->addLayout(finish);
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this); setStyle(style);
    for (auto *widget : findChildren<QWidget *>()) widget->setStyle(style);
    for (auto *button : {settings_, send_, cancel_, apply_, close_}) button->setAutoDefault(false);
    connect(settings_, &QPushButton::clicked, this, [this] {
        auto *settings = new ApiSettingsDialog(this);
        connect(settings, &QDialog::accepted, this, &ApiProofreadDialog::reloadProfile); settings->open();
    });
    connect(send_, &QPushButton::clicked, this, &ApiProofreadDialog::send);
    connect(cancel_, &QPushButton::clicked, &request_, &ApiProofreader::cancel);
    connect(apply_, &QPushButton::clicked, this, &QDialog::accept);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    connect(preview_, &QPlainTextEdit::textChanged, this, &ApiProofreadDialog::updateText);
    connect(&request_, &ApiProofreader::finished, this, [this](const ApiProofreadResult &result) {
        result_ = result; error_ = result.error; hasResult_ = error_.isEmpty() && !result.canceled;
        preview_->setReadOnly(!hasResult_); preview_->setPlainText(hasResult_ ? result.text : QString());
        preview_->document()->setModified(false); updateText();
    });
    connect(&InterfacePreferences::instance(), &InterfacePreferences::changed, this, &ApiProofreadDialog::updateText);
    reloadProfile();
}

void ApiProofreadDialog::reloadProfile() {
    hasResult_ = false; result_ = {}; preview_->clear(); preview_->setReadOnly(true);
    error_.clear(); profile_ = loadApiProfile(&error_); updateText();
}

void ApiProofreadDialog::send() {
    error_.clear(); result_ = {}; hasResult_ = false; preview_->clear(); preview_->setReadOnly(true);
    const auto credential = readApiCredential(apiCredentialAccount(profile_->endpoint));
    if (!credential.error.isEmpty()) { error_ = credential.error; updateText(); return; }
    request_.start(profile_->endpoint, profile_->model, credential.key, text_); updateText();
}

QString ApiProofreadDialog::correctedText() const {
    return preview_->document()->isModified() ? preview_->document()->toRawText().replace(QChar::ParagraphSeparator, '\n') : result_.text;
}

void ApiProofreadDialog::done(int result) { request_.cancel(); QDialog::done(result); }

void ApiProofreadDialog::updateText() {
    const bool busy = request_.busy();
    setWindowTitle(t("API 文字校对"));
    intro_->setText(t("仅在点击发送后提交下方文字。先检查返回结果，再应用到校对稿；API 可能产生费用。"));
    destination_->setText(profile_ ? t("发送至：%1\n模型：%2").arg(profile_->endpoint.toDisplayString(), profile_->model)
                                 : t("尚未配置 API，请先设置服务地址和模型。"));
    sourceLabel_->setText(selection_ ? t("将发送的选中文字") : t("将发送的完整校对稿"));
    resultLabel_->setText(t("返回结果（可编辑）")); source_->setAccessibleName(sourceLabel_->text()); preview_->setAccessibleName(resultLabel_->text());
    preview_->setPlaceholderText(t("发送后在这里预览结果，原稿保持不变。"));
    settings_->setText(t("API 设置…")); send_->setText(t("发送校对")); cancel_->setText(t("取消请求"));
    apply_->setText(t("应用到校对稿")); close_->setText(t("关闭"));
    settings_->setEnabled(!busy); send_->setEnabled(!busy && profile_.has_value() && !text_.isEmpty()); cancel_->setEnabled(busy);
    apply_->setEnabled(!busy && hasResult_ && !preview_->toPlainText().trimmed().isEmpty());
    QString message;
    if (busy) message = t("正在校对…");
    else if (!error_.isEmpty()) message = InterfacePreferences::instance().translatedCaption(error_);
    else if (result_.canceled) message = t("已停止等待。服务可能已处理此次请求，用量未知。可重新发送。");
    else if (hasResult_) message = t("检查结果后点击应用，可在校对稿中撤销。");
    const auto tokens = [](qint64 value) { return value >= 0 ? QString::number(value) : t("未知"); };
    if (hasResult_) message += '\n' + (result_.totalTokens >= 0 || result_.inputTokens >= 0 || result_.outputTokens >= 0
        ? t("用量（token）：输入 %1，输出 %2，合计 %3").arg(tokens(result_.inputTokens), tokens(result_.outputTokens), tokens(result_.totalTokens))
        : t("服务未提供用量。"));
    status_->setText(message);
    auto colors = status_->palette(); colors.setColor(QPalette::WindowText, error_.isEmpty() ? palette().color(QPalette::WindowText)
        : QColor(InterfacePreferences::instance().dark() ? "#ff9b90" : "#b42318")); status_->setPalette(colors);
    for (auto *button : {settings_, send_, cancel_, apply_, close_}) button->setAccessibleName(button->text());
}

} // namespace still
