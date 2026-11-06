#include "scroll_capture_ui.h"
#include "ui/i18n.h"
#include "platform/platform.h"
#include "ui/toolicons.h"

#include <QApplication>
#include <QEvent>
#include <QHideEvent>
#include <QLabel>
#include <QHBoxLayout>
#include <QFrame>
#include <QPainter>
#include <QToolButton>
#include <QShortcut>
#include <QShowEvent>
#include <QScrollArea>
#include <QScrollBar>

namespace still {
namespace {
class ScrollToolbar : public QWidget {
public:
    explicit ScrollToolbar(QWidget *parent) : QWidget(parent) {}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(palette().color(QPalette::Base));
        painter.setPen(palette().color(QPalette::Window).lightness() < 128 ? QColor("#383838") : QColor("#d9dde5"));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    }
};
class ScrollPreview : public QLabel {
public:
    explicit ScrollPreview(QWidget *parent) : QLabel(parent) {}
    qreal visibleFraction = 1;
protected:
    void paintEvent(QPaintEvent *event) override {
        QLabel::paintEvent(event);
        if (pixmap().isNull()) return;
        QPainter painter(this);
        const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setPen(QPen(QColor("#3987ff"), 2)); painter.drawRect(bounds);
        const qreal visible = bounds.height() * visibleFraction;
        painter.setPen(QPen(QColor("#74d65b"), 2));
        painter.drawRect(QRectF(bounds.left(), bounds.bottom() - visible, bounds.width(), visible));
    }
};
class SelectionFrame : public QWidget {
public:
    explicit SelectionFrame(const QRect &region, QWidget *parent)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setObjectName("ScrollSelectionFrame");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setGeometry(region.adjusted(-4, -4, 4, 4));
    }
    bool excluded = false;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const QColor accent = QApplication::palette().color(QPalette::Highlight);
        if (excluded) {
            const QRectF edge = QRectF(rect()).adjusted(4.5, 4.5, -4.5, -4.5);
            painter.setPen(QPen(Qt::white, 4)); painter.drawRect(edge);
            painter.setPen(QPen(accent, 2)); painter.drawRect(edge);
        } else {
            // Leave a gap outside the captured pixels, including fractional DPI rounding.
            painter.fillRect(0, 0, width(), 2, accent);
            painter.fillRect(0, height() - 2, width(), 2, accent);
            painter.fillRect(0, 2, 2, height() - 4, accent);
            painter.fillRect(width() - 2, 2, 2, height() - 4, accent);
        }
    }
};
} // namespace

ScrollCaptureUi::ScrollCaptureUi(const QRect &region, const QRect &available, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      region_(region), available_(available), previewArea_(new QScrollArea(this)), preview_(new ScrollPreview(previewArea_)),
      frame_(new SelectionFrame(region, this)), toolbar_(new ScrollToolbar(this)), dimensions_(new QLabel(toolbar_)) {
    setObjectName("ScrollHud");
    setWindowTitle(t("拾影 · 滚动截图"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    preview_->setObjectName("ScrollImagePreview");
    preview_->setAccessibleName(t("长截图实时预览"));
    preview_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    previewMaximumHeight_ = qMin(360, qMax(1, available.height() - 16));
    setFixedSize(168, previewMaximumHeight_);
    previewArea_->setObjectName("ScrollPreviewArea");
    previewArea_->setFrameShape(QFrame::NoFrame);
    previewArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    previewArea_->setAccessibleName(t("长截图实时预览"));
    previewArea_->setWidget(preview_); preview_->setAutoFillBackground(false);
    previewArea_->viewport()->setAutoFillBackground(false);
    previewArea_->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 8px; background: palette(base); margin: 0; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %2; }"
        "QScrollBar::handle:vertical:disabled { background: transparent; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(dark ? "#898989" : "#858e9e", dark ? "#a8a8a8" : "#454b57"));
    previewArea_->setGeometry(rect());
    auto *bar = previewArea_->verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) { followPreview_ = value >= bar->maximum() - 2; });
    connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int maximum) { if (followPreview_) bar->setValue(maximum); });
    toolbar_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    toolbar_->setAttribute(Qt::WA_ShowWithoutActivating);
    toolbar_->setAttribute(Qt::WA_TranslucentBackground);
    toolbar_->setObjectName("CaptureTools");
    toolbar_->setStyleSheet(toolbarStyleSheet() + QStringLiteral(
        "QWidget#CaptureTools { background: %1; border-radius: 14px; }"
        "#CaptureTools QLabel#ScrollDimensions { color: %2; }"
        "#CaptureTools QLabel#ScrollDirection { background: %3; border-radius: 7px; padding: 4px 8px; }"
        "#CaptureTools QToolButton#scrollAction-copy { background: %4; }"
        "#CaptureTools QToolButton#scrollAction-copy:hover { background: %5; }")
        .arg(dark ? "#1c1c1c" : "#ffffff", dark ? "#adadad" : "#626875",
             dark ? "#292929" : "#f1f3ef", dark ? "#292929" : "#e2e6ed", dark ? "#292929" : "#e2e6ed"));
    QFont body = toolbar_->font();
#ifdef Q_OS_WIN
    body.setFamily("Segoe UI");
#endif
    body.setPixelSize(13); toolbar_->setFont(body);
    auto *layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(7, 5, 7, 5); layout->setSpacing(2);
    dimensions_->setObjectName("ScrollDimensions"); dimensions_->setMinimumWidth(90);
    layout->addWidget(dimensions_);
    auto *direction = new QLabel(t("纵向"), toolbar_); direction->setObjectName("ScrollDirection");
    layout->addWidget(direction); layout->addSpacing(8);
    auto *separator = new QFrame(toolbar_); separator->setProperty("role", "separator"); separator->setFixedSize(1, 22); layout->addWidget(separator);
    const QStringList names{t("编辑"), t("文字"), t("贴图"), t("保存"), t("取消"), t("复制")};
    const QStringList actions{"edit", "ocr", "pin", "save", "cancel", "copy"};
    const QVector<ToolIcon> icons{ToolIcon::Edit, ToolIcon::Ocr, ToolIcon::Pin, ToolIcon::Save, ToolIcon::Close, ToolIcon::Copy};
    const QVector<QKeySequence> keys{QKeySequence(Qt::Key_Return), QKeySequence("Ctrl+O"), QKeySequence("Ctrl+P"), QKeySequence::Save, QKeySequence(Qt::Key_Escape), QKeySequence::Copy};
    for (int i = 0; i < actions.size(); ++i) {
        auto *button = new QToolButton(toolbar_);
        button->setObjectName(actions[i] == "edit" ? "ScrollFinish" : "scrollAction-" + actions[i]);
        button->setText(names[i]); button->setAccessibleName(names[i]);
        button->setIcon(toolIcon(icons[i], actions[i] == "copy" ? QColor(dark ? "#a8a8a8" : "#454b57") : palette().color(QPalette::WindowText)));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(names[i] + " (" + keys[i].toString(QKeySequence::NativeText) + ')');
        layout->addWidget(button);
        const auto request = [this, action = actions[i]] { emit actionRequested(action); emit finishRequested(); };
        connect(button, &QToolButton::clicked, this, request);
        auto *shortcut = new QShortcut(keys[i], toolbar_); connect(shortcut, &QShortcut::activated, this, request);
    }
    toolbar_->adjustSize(); toolbar_->setFixedSize(toolbar_->sizeHint());
}

bool ScrollCaptureUi::prepare() {
    const auto place = [this](QWidget *window, const QPoint &preferred) {
        if (window->width() > available_.width() - 16 || window->height() > available_.height() - 16) return false;
        window->move(qBound(available_.left() + 8, preferred.x(), available_.right() - window->width() - 7),
                     qBound(available_.top() + 8, preferred.y(), available_.bottom() - window->height() - 7));
        if (excludeWindowFromCapture(quintptr(window->winId())) || !window->geometry().intersects(region_)) return true;
        const QVector<QPoint> alternatives{{region_.right() + 12, window->y()},
            {region_.left() - window->width() - 12, window->y()},
            {window->x(), region_.bottom() + 12}, {window->x(), region_.top() - window->height() - 12}};
        for (const auto &point : alternatives) {
            const QRect bounds(point, window->size());
            if (available_.contains(bounds) && !bounds.intersects(region_)) { window->move(point); return true; }
        }
        return false;
    };
    auto *selection = static_cast<SelectionFrame *>(frame_);
    selection->excluded = excludeWindowFromCapture(quintptr(frame_->winId()));
    return place(this, {region_.right() + 12, region_.top()}) &&
        place(toolbar_, {region_.right() - toolbar_->width() + 1, region_.bottom() + 12});
}

void ScrollCaptureUi::setImage(const QImage &image) {
    if (image.isNull()) return;
    if (!viewportHeight_) viewportHeight_ = image.height();
    const qreal ratio = devicePixelRatioF();
    const int width = qMin(image.width(), qRound(160 * ratio));
    // Keep a fixed pixel scale; the scroll area clips long content instead of shrinking it.
    QImage thumbnail = image.width() > width * 2 ? image.scaledToWidth(width * 2, Qt::FastTransformation) : image;
    thumbnail = thumbnail.scaledToWidth(width, Qt::SmoothTransformation);
    thumbnail.setDevicePixelRatio(ratio);
    preview_->setFixedSize((thumbnail.size() / ratio).expandedTo(QSize(1, 1)));
    preview_->setPixmap(QPixmap::fromImage(thumbnail));
    setFixedHeight(qMin(previewMaximumHeight_, preview_->height()));
    previewArea_->setGeometry(rect());
    static_cast<ScrollPreview *>(preview_)->visibleFraction = qMin(1.0, qreal(viewportHeight_) / image.height());
    dimensions_->setText(QStringLiteral("%1 × %2").arg(image.width()).arg(image.height()));
    dimensions_->setAccessibleName(t("%1 × %2 像素").arg(image.width()).arg(image.height()));
    preview_->setAccessibleDescription({});
}
void ScrollCaptureUi::showAlignmentPending() { preview_->setAccessibleDescription(t("请暂停滚动，或稍向上滚动以对齐画面")); }
void ScrollCaptureUi::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange && frame_) frame_->update();
}
void ScrollCaptureUi::showEvent(QShowEvent *event) { QWidget::showEvent(event); frame_->show(); toolbar_->show(); raise(); toolbar_->raise(); }
void ScrollCaptureUi::hideEvent(QHideEvent *event) { frame_->hide(); toolbar_->hide(); QWidget::hideEvent(event); }

} // namespace still
