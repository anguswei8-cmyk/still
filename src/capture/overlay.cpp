#include "ui/i18n.h"
#include "overlay.h"
#include "imaging/image.h"
#include "ui/toolicons.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyleOptionMenuItem>
#include <QToolButton>
#include <QVBoxLayout>

namespace still {

Overlay::Overlay(CaptureFrame frame, QString mode, bool magnifier)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint), frame_(std::move(frame)), mode_(std::move(mode)), magnifier_(magnifier), toolbar_(new QWidget(this)),
      widthInput_(new QSpinBox(toolbar_)), heightInput_(new QSpinBox(toolbar_)), ratioInput_(new QComboBox(toolbar_)) {
    setAttribute(Qt::WA_DeleteOnClose);
    setGeometry(frame_.geometry);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(t("拾影 · 选择截图区域"));
    setAccessibleName(t("拖动框选 / 点击选窗口 · Shift 松手调整 · Enter 确认 · Esc 取消"));
    toolbar_->setObjectName("CaptureTools");
    toolbar_->setAttribute(Qt::WA_StyledBackground);
    toolbar_->setAttribute(Qt::WA_NoMousePropagation);
    auto *rows = new QVBoxLayout(toolbar_);
    rows->setContentsMargins(7, 5, 7, 5); rows->setSpacing(4);
    auto *dimensions = new QHBoxLayout;
    dimensions->setSpacing(4);
    widthInput_->setObjectName("CaptureWidth"); widthInput_->setRange(1, frame_.image.width());
    heightInput_->setObjectName("CaptureHeight"); heightInput_->setRange(1, frame_.image.height());
    widthInput_->setAccessibleName(t("选区宽度（像素）")); heightInput_->setAccessibleName(t("选区高度（像素）"));
    widthInput_->setKeyboardTracking(false); heightInput_->setKeyboardTracking(false);
    widthInput_->setMinimumWidth(72); heightInput_->setMinimumWidth(72);
    auto *widthLabel = new QLabel(t("宽"), toolbar_); widthLabel->setBuddy(widthInput_);
    auto *heightLabel = new QLabel(t("高"), toolbar_); heightLabel->setBuddy(heightInput_);
    dimensions->addWidget(widthLabel); dimensions->addWidget(widthInput_);
    dimensions->addWidget(heightLabel); dimensions->addWidget(heightInput_);
    dimensions->addWidget(new QLabel(t("像素"), toolbar_)); dimensions->addStretch();
    ratioInput_->setObjectName("CaptureAspectRatio");
    ratioInput_->setAccessibleName(t("截图比例"));
    ratioInput_->setToolTip(t("固定比例时，宽高按整数像素倍数对齐。"));
    ratioInput_->addItem(t("自由比例"), QSize(0, 0));
    for (const QSize ratio : {QSize(1, 1), QSize(4, 3), QSize(16, 9), QSize(3, 4), QSize(9, 16)}) {
        if (ratio.width() <= frame_.image.width() && ratio.height() <= frame_.image.height())
            ratioInput_->addItem(QString("%1:%2").arg(ratio.width()).arg(ratio.height()), ratio);
    }
    dimensions->addWidget(ratioInput_);
    rows->addLayout(dimensions);
    auto *actionsRow = new QWidget(toolbar_); actionsRow->setObjectName("CaptureActions");
    auto *layout = new QHBoxLayout(actionsRow);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    rows->addWidget(actionsRow);
    setTabOrder(widthInput_, heightInput_);
    setTabOrder(heightInput_, ratioInput_);
    connect(widthInput_, &QSpinBox::valueChanged, this, [this](int value) { resizeSelection(value, Qt::Horizontal); });
    connect(heightInput_, &QSpinBox::valueChanged, this, [this](int value) { resizeSelection(value, Qt::Vertical); });
    connect(ratioInput_, &QComboBox::currentIndexChanged, this, [this] {
        if (ratioInput_->currentData().toSize().isEmpty()) placeToolbar();
        else resizeSelection(widthInput_->value(), Qt::Horizontal);
    });
    const QVector<QPair<QString, QString>> actions{{t("编辑"), "edit"}, {t("复制"), "copy"},
        {t("保存"), "save"}, {t("快速保存"), "quickSave"}, {t("贴图"), "pin"}, {t("文字"), "ocr"},
        {t("长截图"), "scroll"}, {t("取消"), "cancel"}};
    const QVector<ToolIcon> icons{ToolIcon::Edit, ToolIcon::Copy, ToolIcon::Save, ToolIcon::QuickSave, ToolIcon::Pin, ToolIcon::Ocr, ToolIcon::Scroll, ToolIcon::Close};
    const QVector<QKeySequence> shortcuts{QKeySequence(Qt::Key_Return), QKeySequence::Copy, QKeySequence::Save,
        QKeySequence("Ctrl+Shift+S"), QKeySequence("Ctrl+P"), QKeySequence("Ctrl+O"), {}, QKeySequence(Qt::Key_Escape)};
    for (int i = 0; i < actions.size(); ++i) {
        if (i == 1 || actions[i].second == "cancel") {
            auto *separator = new QFrame(actionsRow);
            separator->setProperty("role", "separator");
            separator->setFixedSize(1, 20);
            layout->addSpacing(4);
            layout->addWidget(separator);
            layout->addSpacing(4);
        }
        const auto action = actions[i];
        auto *button = new QToolButton(actionsRow);
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
    // The QSS spin-button fallback leaves no room for arrows at Cocoa's default width.
    const auto stepButtons = QStringLiteral("QSpinBox::up-button, QSpinBox::down-button { width: 18px; }");
    widthInput_->setStyleSheet(stepButtons); heightInput_->setStyleSheet(stepButtons);
    QStyleOptionMenuItem item; item.initFrom(ratioInput_);
    item.menuItemType = QStyleOptionMenuItem::Normal; item.checkType = QStyleOptionMenuItem::NonExclusive;
    item.font = ratioInput_->font(); item.fontMetrics = QFontMetrics(item.font); item.maxIconWidth = ratioInput_->iconSize().width() + 4;
    int popupWidth = 0;
    for (int i = 0; i < ratioInput_->count(); ++i) {
        item.text = ratioInput_->itemText(i);
        // The Mac menu style expects its caller to include the text extent.
        const QSize textSize = item.fontMetrics.size(Qt::TextSingleLine, item.text);
        popupWidth = qMax(popupWidth, ratioInput_->style()->sizeFromContents(QStyle::CT_MenuItem, &item, textSize, ratioInput_).width());
    }
    ratioInput_->view()->setMinimumWidth(popupWidth);
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
    const QRectF selected = selection_.isEmpty() ? hoverSelection() : selection_;
    if (!selected.isEmpty()) {
        painter.save();
        painter.setClipRect(selected);
        painter.drawImage(rect(), frame_.image);
        painter.restore();
        painter.setPen(QPen(QColor("#64b5ff"), 1));
        painter.drawRect(selected);
        if (!selection_.isEmpty()) for (const auto &point : handles()) painter.fillRect(QRectF(point - QPointF(3, 3), QSizeF(6, 6)), QColor("#64b5ff"));
        if (!toolbar_->isVisible()) {
            const QRect pixels = pixelRect(selected, size(), frame_.image.size());
            painter.fillRect(QRectF(selected.x(), qMax(0.0, selected.y() - 27), 240, 25), QColor("#202630"));
            painter.setPen(Qt::white);
            painter.drawText(QRectF(selected.x() + 6, qMax(0.0, selected.y() - 27), 230, 25), Qt::AlignVCenter,
                             t("%1 × %2 像素").arg(pixels.width()).arg(pixels.height()));
        }
    }
    if (!toolbar_->isVisible()) {
        painter.setPen(Qt::white);
        painter.fillRect(QRect(12, 12, qMin(760, width() - 24), 30), QColor("#202630"));
        painter.drawText(QRect(20, 12, width() - 40, 30), Qt::AlignVCenter,
                         t("拖动框选 / 点击选窗口 · Shift 松手调整 · Enter 确认 · Esc 取消"));
    }
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
    setFocus(Qt::MouseFocusReason);
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
        if (!ratioInput_->currentData().toSize().isEmpty()) dragRatioSelection(end);
        else if (handle_ < 0) selection_ = QRectF(start_, end).normalized();
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
    const bool fixedRatio = !ratioInput_->currentData().toSize().isEmpty();
    const bool clicked = fixedRatio ? QLineF(start_, event->position()).length() < 3 : selection_.width() < 3 || selection_.height() < 3;
    if (handle_ < 0 && clicked) selection_ = hoverSelection();
    if (selection_.isEmpty()) { update(); return; }
    if (handle_ < 0 && (mode_ == "copy" || mode_ == "scroll") && !(event->modifiers() & Qt::ShiftModifier)) { accept(mode_); return; }
    placeToolbar();
}

void Overlay::placeToolbar() {
    if (selection_.isEmpty()) { toolbar_->hide(); update(); return; }
    const QRect pixels = pixelRect(selection_, size(), frame_.image.size());
    const QSignalBlocker widthSignals(widthInput_), heightSignals(heightInput_);
    const QSize ratio = ratioInput_->currentData().toSize();
    if (ratio.isEmpty()) {
        widthInput_->setRange(1, frame_.image.width()); heightInput_->setRange(1, frame_.image.height());
        widthInput_->setSingleStep(1); heightInput_->setSingleStep(1);
    } else {
        const int maximum = qMin(frame_.image.width() / ratio.width(), frame_.image.height() / ratio.height());
        widthInput_->setRange(ratio.width(), maximum * ratio.width()); heightInput_->setRange(ratio.height(), maximum * ratio.height());
        widthInput_->setSingleStep(ratio.width()); heightInput_->setSingleStep(ratio.height());
    }
    widthInput_->setValue(pixels.width()); heightInput_->setValue(pixels.height());
    toolbar_->adjustSize();
    const int top = selection_.bottom() + toolbar_->height() + 8 < height() ? qRound(selection_.bottom()) + 8 : qMax(0, qRound(selection_.top()) - toolbar_->height() - 8);
    toolbar_->move(qBound(0, qRound(selection_.right()) - toolbar_->width(), qMax(0, width() - toolbar_->width())), top);
    toolbar_->show();
    update();
}

void Overlay::resizeSelection(int value, Qt::Orientation axis) {
    QRect pixels = pixelRect(selection_, size(), frame_.image.size());
    if (axis == Qt::Horizontal) pixels.setWidth(value); else pixels.setHeight(value);
    pixels.setSize(ratioSize(pixels.size(), axis));
    selection_ = logicalSelection(pixels);
    placeToolbar();
}

QSize Overlay::ratioSize(const QSize &wanted, Qt::Orientation axis) const {
    const QSize ratio = ratioInput_->currentData().toSize();
    if (ratio.isEmpty()) return wanted;
    const int maximum = qMin(frame_.image.width() / ratio.width(), frame_.image.height() / ratio.height());
    const qreal units = axis == Qt::Horizontal ? qreal(wanted.width()) / ratio.width() : qreal(wanted.height()) / ratio.height();
    return ratio * qBound(1, qRound(units), maximum);
}

QRectF Overlay::logicalSelection(QRect pixels) const {
    pixels.moveLeft(qBound(0, pixels.left(), frame_.image.width() - pixels.width()));
    pixels.moveTop(qBound(0, pixels.top(), frame_.image.height() - pixels.height()));
    return logicalRect(pixels, size(), frame_.image.size());
}

QRectF Overlay::hoverSelection() const {
    if (hovered_.isEmpty() || ratioInput_->currentData().toSize().isEmpty()) return hovered_;
    QRect pixels = pixelRect(hovered_, size(), frame_.image.size());
    pixels.setSize(ratioSize(pixels.size(), Qt::Horizontal));
    return logicalSelection(pixels);
}

void Overlay::dragRatioSelection(const QPointF &end) {
    const qreal sx = qreal(frame_.image.width()) / width(), sy = qreal(frame_.image.height()) / height();
    const QRect initial = pixelRect(initialSelection_, size(), frame_.image.size());
    if (handle_ == 8) {
        selection_ = logicalSelection(initial.translated(qRound((end.x() - start_.x()) * sx), qRound((end.y() - start_.y()) * sy)));
        return;
    }
    const QSize ratio = ratioInput_->currentData().toSize();
    const QPoint pointer(qRound(end.x() * sx), qRound(end.y() * sy));
    const bool centerX = handle_ == 1 || handle_ == 5;
    const bool centerY = handle_ == 3 || handle_ == 7;
    QPointF anchor(qRound(start_.x() * sx), qRound(start_.y() * sy));
    if (handle_ >= 0) {
        const bool fromLeft = handle_ == 0 || handle_ == 6 || handle_ == 7;
        const bool fromTop = handle_ == 0 || handle_ == 1 || handle_ == 2;
        anchor.setX(centerX ? initial.x() + initial.width() / 2.0 : fromLeft ? initial.x() + initial.width() : initial.x());
        anchor.setY(centerY ? initial.y() + initial.height() / 2.0 : fromTop ? initial.y() + initial.height() : initial.y());
    }
    const bool left = pointer.x() < anchor.x(), up = pointer.y() < anchor.y();
    const qreal availableWidth = centerX ? 2 * qMin(anchor.x(), frame_.image.width() - anchor.x()) : left ? anchor.x() : frame_.image.width() - anchor.x();
    const qreal availableHeight = centerY ? 2 * qMin(anchor.y(), frame_.image.height() - anchor.y()) : up ? anchor.y() : frame_.image.height() - anchor.y();
    const int maximum = qFloor(qMin(availableWidth / ratio.width(), availableHeight / ratio.height()));
    // Keep the last valid rectangle if the crossed side cannot fit one ratio unit.
    if (maximum < 1) return;
    const qreal horizontal = qAbs(pointer.x() - anchor.x()) / ratio.width(), vertical = qAbs(pointer.y() - anchor.y()) / ratio.height();
    const int units = qBound(1, qRound(centerX ? vertical : centerY ? horizontal : qMax(horizontal, vertical)), maximum);
    const QSize extent = ratio * units;
    const int x = qRound(anchor.x() - extent.width() * (centerX ? 0.5 : left ? 1.0 : 0.0));
    const int y = qRound(anchor.y() - extent.height() * (centerY ? 0.5 : up ? 1.0 : 0.0));
    selection_ = logicalSelection(QRect(QPoint(x, y), extent));
}

void Overlay::accept(const QString &action) {
    if (selection_.isEmpty()) return;
    widthInput_->interpretText(); heightInput_->interpretText();
    const QImage image = cropFrame(frame_.image, size(), selection_);
    if (image.isNull()) return;
    finished_ = true;
    emit accepted(image, selection_, action);
}

void Overlay::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { close(); return; }
    if (widthInput_->hasFocus() || heightInput_->hasFocus() || ratioInput_->hasFocus()) { event->ignore(); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { accept(mode_ == "annotate" ? "edit" : mode_); return; }
    if (event->matches(QKeySequence::Copy)) { accept("copy"); return; }
    if (QKeySequence(event->keyCombination()) == QKeySequence("Ctrl+Shift+S")) { accept("quickSave"); return; }
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
            const QSize ratio = ratioInput_->currentData().toSize();
            if (!ratio.isEmpty()) {
                QRect pixels = pixelRect(selection_, size(), frame_.image.size());
                if (event->modifiers() & Qt::AltModifier) pixels.translate(delta);
                else {
                    const QSize wanted(pixels.width() + delta.x() * ratio.width(), pixels.height() + delta.y() * ratio.height());
                    pixels.setSize(ratioSize(wanted, delta.x() ? Qt::Horizontal : Qt::Vertical));
                }
                selection_ = logicalSelection(pixels);
            } else {
                if (event->modifiers() & Qt::AltModifier) selection_.translate(delta);
                else selection_.setBottomRight(selection_.bottomRight() + delta);
                selection_ = selection_.normalized().intersected(QRectF(rect()));
            }
            placeToolbar();
        }
    }
}

void Overlay::closeEvent(QCloseEvent *event) {
    if (!finished_) emit cancelled();
    event->accept();
}

} // namespace still
