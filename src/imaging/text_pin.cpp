#include "text_pin.h"
#include "html_document.h"
#include "ui/i18n.h"
#include "ui/toolicons.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSizeGrip>
#include <QTextEdit>
#include <QToolButton>
#include <QTextDocumentFragment>
#include <QVBoxLayout>

namespace still {

TextPin::TextPin(QString text, QString html) : text_(std::move(text)), html_(std::move(html)), editor_(new QTextEdit(this)) {
    setWindowTitle(html_.isEmpty() ? t("拾影 · 文字贴图") : t("拾影 · 富文本贴图"));
    setAccessibleName(html_.isEmpty() ? t("可选择和滚动的纯文本贴图") : t("可选择和滚动的富文本贴图"));
    setMinimumSize(240, 160); resize(400, 320); originalSize_ = size();
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto available = screen->availableGeometry();
    move(qBound(available.left(), QCursor::pos().x() - width() / 2, available.right() - width() + 1),
         qBound(available.top(), QCursor::pos().y() - 20, available.bottom() - height() + 1));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 6); layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(html_.isEmpty() ? t("文字贴图") : t("富文本贴图"), this); title->setCursor(Qt::SizeAllCursor); header->addWidget(title); header->addStretch();
    auto *close = new QToolButton(this); close->setObjectName("TextPinClose"); close->setIcon(toolIcon(ToolIcon::Close)); close->setAutoRaise(true);
    close->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; }"
        "QToolButton:hover, QToolButton:pressed { background: palette(alternate-base); } QToolButton:focus { border-color: palette(text); }"));
    close->setToolTip(t("关闭")); close->setAccessibleName(t("关闭文字贴图")); close->setFixedSize(24, 24);
    connect(close, &QToolButton::clicked, this, &QWidget::close); header->addWidget(close); layout->addLayout(header);
    editor_->setObjectName("TextPinContent"); editor_->setReadOnly(true); editor_->setAcceptRichText(false);
    editor_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    editor_->setAccessibleName(t("贴图文字")); editor_->setFrameShape(QFrame::NoFrame);
    editor_->setStyleSheet(QStringLiteral("QScrollBar:vertical { width: 8px; background: palette(base); margin: 0; }"
        "QScrollBar::handle:vertical { background: palette(text); border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    auto font = editor_->font(); font.setPixelSize(14); editor_->setFont(font);
    if (html_.isEmpty()) {
        // QTextDocument normalizes CRLF; map its selection boundaries back to the original text.
        QString displayed; displayed.reserve(text_.size()); sourceOffsets_.reserve(text_.size() + 1);
        sourceOffsets_.append(0);
        for (qsizetype i = 0; i < text_.size(); ++i) {
            const auto c = text_[i];
            displayed += c == '\r' ? QChar('\n') : c;
            if (c == '\r' && i + 1 < text_.size() && text_[i + 1] == '\n') ++i;
            sourceOffsets_.append(i + 1);
        }
        editor_->setPlainText(displayed);
    } else {
        auto *document = new SafeHtmlDocument(html_, editor_); document->setDefaultFont(font); editor_->setDocument(document);
        editor_->setAccessibleDescription(t("仅显示文字排版，不加载图片或外部资源"));
    }
    editor_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editor_, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) { showMenu(editor_->mapToGlobal(point)); });
    editor_->installEventFilter(this); layout->addWidget(editor_, 1);
    auto *footer = new QHBoxLayout;
    auto *hint = new QLabel(html_.isEmpty() ? t("纯文本 · 拖动标题移动") : t("富文本 · 拖动标题移动"), this); auto small = hint->font(); small.setPixelSize(12); hint->setFont(small);
    if (!html_.isEmpty()) hint->setToolTip(t("仅显示文字排版，不加载图片或外部资源"));
    footer->addWidget(hint); footer->addStretch(); footer->addWidget(new QSizeGrip(this)); layout->addLayout(footer);
    editor_->setFocus();
}

PinState TextPin::state() const { auto result = layoutState(); result.text = text_; result.html = html_; return result; }

void TextPin::copy() {
    const auto cursor = editor_->textCursor();
    if (!html_.isEmpty()) {
        QApplication::clipboard()->setText(cursor.hasSelection() ? cursor.selection().toPlainText() : editor_->toPlainText());
        return;
    }
    const QString selected = cursor.hasSelection() ? text_.mid(sourceOffsets_[cursor.selectionStart()], sourceOffsets_[cursor.selectionEnd()] - sourceOffsets_[cursor.selectionStart()]) : text_;
    QApplication::clipboard()->setText(selected);
}

bool TextPin::eventFilter(QObject *object, QEvent *event) {
    if (object == editor_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::Copy)) { copy(); return true; }
        if (key->key() == Qt::Key_Escape) { close(); return true; }
    }
    return PinWindow::eventFilter(object, event);
}

void TextPin::showMenu(const QPoint &point) {
    QMenu menu(this); menu.setObjectName("TextPinMenu");
    menu.addAction(t("复制"), this, &TextPin::copy);
    menu.addAction(t("全选"), editor_, &QTextEdit::selectAll);
    auto *more = menu.addMenu(t("更多操作"));
    auto *top = more->addAction(t("保持置顶")); top->setCheckable(true); top->setChecked(windowFlags() & Qt::WindowStaysOnTopHint);
    connect(top, &QAction::triggered, this, [this](bool checked) { setWindowFlag(Qt::WindowStaysOnTopHint, checked); show(); });
    auto *restore = more->addAction(t("恢复最近关闭贴图"), this, &PinWindow::restoreClosedRequested); restore->setEnabled(canRestoreClosed_);
    more->addAction(t("隐藏/显示全部贴图"), this, &PinWindow::toggleAllRequested);
    more->addAction(t("关闭全部贴图"), this, &PinWindow::closeAllRequested);
    menu.addSeparator(); menu.addAction(t("关闭"), this, &QWidget::close); menu.exec(point);
}

void TextPin::contextMenuEvent(QContextMenuEvent *event) { showMenu(event->globalPos()); }
void TextPin::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && event->position().y() < 36) { moving_ = true; dragOffset_ = event->globalPosition().toPoint() - pos(); }
}
void TextPin::mouseMoveEvent(QMouseEvent *event) { if (moving_) move(event->globalPosition().toPoint() - dragOffset_); }
void TextPin::mouseReleaseEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) moving_ = false; }
void TextPin::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.setPen(palette().color(QPalette::Mid)); painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

} // namespace still
