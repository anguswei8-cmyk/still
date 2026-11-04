#include "ui/i18n.h"
#include "overlay.h"
#include "imaging/image.h"
#include "ui/toolicons.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

namespace pixclone {

Overlay::Overlay(CaptureFrame frame, QString mode, bool magnifier)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), frame_(std::move(frame)), mode_(std::move(mode)), magnifier_(magnifier), toolbar_(new QWidget(this)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setGeometry(frame_.geometry);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(t("拾影 · 选择截图区域"));
    setAccessibleName(t("拖动选择截图区域，Enter 确认，Esc 取消"));
    toolbar_->setObjectName("CaptureTools");
    toolbar_->setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(7, 5, 7, 5);
    layout->setSpacing(2);
    const QVector<QPair<QString, QString>> actions{{t("编辑"), "edit"}, {t("复制"), "copy"},
        {t("保存"), "save"}, {t("贴图"), "pin"}, {t("文字"), "ocr"},
        {t("长截图"), "scroll"}, {t("取消"), "cancel"}};
    const QVector<ToolIcon> icons{ToolIcon::Edit, ToolIcon::Copy, ToolIcon::Save, ToolIcon::Pin, ToolIcon::Ocr, ToolIcon::Scroll, ToolIcon::Close};
    const QVector<QKeySequence> shortcuts{QKeySequence(Qt::Key_Return), QKeySequence::Copy, QKeySequence::Save,
        QKeySequence("Ctrl+P"), QKeySequence("Ctrl+O"), {}, QKeySequence(Qt::Key_Escape)};
    for (int i = 0; i < actions.size(); ++i) {
        if (i == 1 || i == 6) {
            auto *separator = new QFrame(toolbar_);
            separator->setProperty("role", "separator");
            separator->setFixedSize(1, 20);
            layout->addSpacing(4);
            layout->addWidget(separator);
            layout->addSpacing(4);
        }
        const auto action = actions[i];
        auto *button = new QToolButton(toolbar_);
        button->setObjectName("captureAction-" + action.second);
        button->setText(action.first);
        button->setAccessibleName(action.first);
        button->setIcon(toolIcon(icons[i]));
        button->setIconSize(QSize(20, 20));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        const QString shortcut = shortcuts[i].toString(QKeySequence::NativeText);
        button->setToolTip(shortcut.isEmpty() ? action.first : action.first + " (" + shortcut + ')');
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, action] { if (action.second == "cancel") close(); else accept(action.second); });
    }
    toolbar_->setStyleSheet(toolbarStyleSheet());
    toolbar_->hide();
}

QVector<QPointF> Overlay::handles() const {
    const auto &r = selection_;
    return {r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(), QPointF(r.right(), r.center().y()),
            r.bottomRight(), QPointF(r.center().x(), r.bottom()), r.bottomLeft(), QPointF(r.left(), r.center().y())};
}

void Overlay::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.drawImage(rect(), frame_.image);
    painter.fillRect(rect(), QColor(0, 0, 0, 115));
    const QRectF selected = selection_.isEmpty() ? QRectF(hovered_) : selection_;
    if (!selected.isEmpty()) {
        painter.save();
        painter.setClipRect(selected);
        painter.drawImage(rect(), frame_.image);
        painter.restore();
        painter.setPen(QPen(QColor("#64b5ff"), 1));
        painter.drawRect(selected);
        if (!selection_.isEmpty()) for (const auto &point : handles()) painter.fillRect(QRectF(point - QPointF(3, 3), QSizeF(6, 6)), QColor("#64b5ff"));
        const QRect pixels = pixelRect(selected, size(), frame_.image.size());
        painter.fillRect(QRectF(selected.x(), qMax(0.0, selected.y() - 27), 240, 25), QColor("#202630"));
        painter.setPen(Qt::white);
        painter.drawText(QRectF(selected.x() + 6, qMax(0.0, selected.y() - 27), 230, 25), Qt::AlignVCenter,
                         t("%1 × %2 像素").arg(pixels.width()).arg(pixels.height()));
    }
    painter.setPen(Qt::white);
    painter.fillRect(QRect(12, 12, qMin(760, width() - 24), 30), QColor("#202630"));
    painter.drawText(QRect(20, 12, width() - 40, 30), Qt::AlignVCenter,
                     t("拖动框选 / 点击选窗口 · Enter 确认 · Ctrl/Cmd+P 贴图 · Esc 取消"));
    if (magnifier_ && !dragging_ && selection_.isEmpty()) {
        const QPointF source(cursor_.x() * frame_.image.width() / width(), cursor_.y() * frame_.image.height() / height());
        const QRect area(qRound(source.x()) - 8, qRound(source.y()) - 8, 16, 16);
        const QPoint at(qBound(0, qRound(cursor_.x()) + 24, qMax(0, width() - 128)), qBound(0, qRound(cursor_.y()) + 24, qMax(0, height() - 156)));
        painter.drawImage(QRect(at, QSize(128, 128)), frame_.image.copy(area));
        painter.setPen(QPen(Qt::red, 1));
        painter.drawLine(at + QPoint(64, 0), at + QPoint(64, 128));
        painter.drawLine(at + QPoint(0, 64), at + QPoint(128, 64));
        painter.fillRect(QRect(at + QPoint(0, 128), QSize(128, 25)), QColor("#202630"));
        painter.setPen(Qt::white);
        const QColor color = frame_.image.pixelColor(qBound(0, qRound(source.x()), frame_.image.width() - 1), qBound(0, qRound(source.y()), frame_.image.height() - 1));
        painter.drawText(QRect(at + QPoint(0, 128), QSize(128, 25)), Qt::AlignCenter, color.name() + " · C");
    }
}

void Overlay::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton) { selection_ = {}; toolbar_->hide(); update(); return; }
    if (event->button() != Qt::LeftButton) return;
    start_ = cursor_ = event->position();
    initialSelection_ = selection_;
    handle_ = -1;
    if (!selection_.isEmpty()) {
        const auto points = handles();
        for (int i = 0; i < points.size(); ++i) if (QLineF(points[i], start_).length() < 10) { handle_ = i; break; }
        if (handle_ < 0 && selection_.contains(start_)) handle_ = 8;
    }
    if (handle_ < 0) selection_ = {};
    dragging_ = true;
    toolbar_->hide();
}

void Overlay::mouseMoveEvent(QMouseEvent *event) {
    cursor_ = event->position();
    if (dragging_) {
        const QPointF end(qBound(0.0, cursor_.x(), qreal(width())), qBound(0.0, cursor_.y(), qreal(height())));
        if (handle_ < 0) selection_ = QRectF(start_, end).normalized();
        else if (handle_ == 8) {
            selection_ = initialSelection_.translated(end - start_);
            selection_.moveLeft(qBound(0.0, selection_.left(), width() - selection_.width()));
            selection_.moveTop(qBound(0.0, selection_.top(), height() - selection_.height()));
        } else {
            selection_ = initialSelection_;
            if (handle_ == 0 || handle_ == 6 || handle_ == 7) selection_.setLeft(end.x());
            if (handle_ == 2 || handle_ == 3 || handle_ == 4) selection_.setRight(end.x());
            if (handle_ == 0 || handle_ == 1 || handle_ == 2) selection_.setTop(end.y());
            if (handle_ == 4 || handle_ == 5 || handle_ == 6) selection_.setBottom(end.y());
            selection_ = selection_.normalized();
        }
    } else if (selection_.isEmpty()) {
        hovered_ = {};
        for (const auto &window : frame_.windows) if (window.contains(cursor_.toPoint())) { hovered_ = window; break; }
    }
    update();
}

void Overlay::mouseReleaseEvent(QMouseEvent *event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    mouseMoveEvent(event);
    dragging_ = false;
    if (selection_.width() < 3 || selection_.height() < 3) selection_ = hovered_;
    if (selection_.isEmpty()) { update(); return; }
    if (mode_ == "copy" || mode_ == "scroll") { accept(mode_); return; }
    placeToolbar();
}

void Overlay::placeToolbar() {
    toolbar_->adjustSize();
    const int top = selection_.bottom() + toolbar_->height() + 8 < height() ? qRound(selection_.bottom()) + 8 : qMax(0, qRound(selection_.top()) - toolbar_->height() - 8);
    toolbar_->move(qBound(0, qRound(selection_.right()) - toolbar_->width(), qMax(0, width() - toolbar_->width())), top);
    toolbar_->show();
    update();
}

void Overlay::accept(const QString &action) {
    if (selection_.isEmpty()) return;
    const QImage image = cropFrame(frame_.image, size(), selection_);
    if (image.isNull()) return;
    finished_ = true;
    emit accepted(image, selection_.toAlignedRect().translated(frame_.geometry.topLeft()), action);
}

void Overlay::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { close(); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { accept(mode_ == "annotate" ? "edit" : mode_); return; }
    if (event->matches(QKeySequence::Copy)) { accept("copy"); return; }
    if (event->matches(QKeySequence::Save)) { accept("save"); return; }
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_P) { accept("pin"); return; }
        if (event->key() == Qt::Key_O) { accept("ocr"); return; }
    }
    if (event->key() == Qt::Key_C) {
        const int x = qBound(0, qRound(cursor_.x() * frame_.image.width() / width()), frame_.image.width() - 1);
        const int y = qBound(0, qRound(cursor_.y() * frame_.image.height() / height()), frame_.image.height() - 1);
        QApplication::clipboard()->setText(frame_.image.pixelColor(x, y).name());
        return;
    }
    if (!selection_.isEmpty()) {
        const int step = event->modifiers() & Qt::ShiftModifier ? 10 : 1;
        QPoint delta;
        if (event->key() == Qt::Key_Left) delta.setX(-step);
        if (event->key() == Qt::Key_Right) delta.setX(step);
        if (event->key() == Qt::Key_Up) delta.setY(-step);
        if (event->key() == Qt::Key_Down) delta.setY(step);
        if (!delta.isNull()) {
            if (event->modifiers() & Qt::AltModifier) selection_.translate(delta);
            else selection_.setBottomRight(selection_.bottomRight() + delta);
            selection_ = selection_.normalized().intersected(QRectF(rect()));
            placeToolbar();
        }
    }
}

void Overlay::closeEvent(QCloseEvent *event) {
    if (!finished_) emit cancelled();
    event->accept();
}

} // namespace pixclone
