#include "ui/i18n.h"
#include "pin.h"
#include "editor.h"
#include "ocr.h"
#include "ocr_result_dialog.h"
#include "storage/image_export.h"
#include "clipboard/image_copy.h"

#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScopeGuard>
#include <QStyleOptionButton>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>

namespace still {
namespace {
class PromptCheckBox : public QCheckBox {
public:
    using QCheckBox::QCheckBox;
protected:
    void paintEvent(QPaintEvent *event) override {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option; initStyleOption(&option);
        const QRect indicator = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        // The Windows native tick disappears on a translucent popup; keep native hit testing and text.
        const QColor accent = palette().color(QPalette::Highlight);
        painter.setBrush(isChecked() ? accent : palette().color(QPalette::Base));
        painter.setPen(isChecked() ? accent : palette().color(QPalette::Text));
        painter.drawRoundedRect(QRectF(indicator).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (isChecked()) {
            painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const QPointF origin = indicator.topLeft();
            painter.drawPolyline(QPolygonF{origin + QPointF(indicator.width() * .23, indicator.height() * .52),
                origin + QPointF(indicator.width() * .43, indicator.height() * .72),
                origin + QPointF(indicator.width() * .78, indicator.height() * .30)});
        }
    }
};
class PinClosePrompt : public QDialog {
public:
    explicit PinClosePrompt(QWidget *parent)
        : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          skip(new PromptCheckBox(t("下次不再提示"), this)) {
        setObjectName("PinClosePrompt"); setWindowTitle(t("关闭贴图"));
        setWindowModality(Qt::NonModal); setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_TranslucentBackground); setAttribute(Qt::WA_ShowWithoutActivating);
        QFont body = font();
#ifdef Q_OS_WIN
        body.setFamily("Microsoft YaHei UI");
#endif
        body.setPixelSize(13); setFont(body);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(18, 16, 18, 16); layout->setSpacing(14);
        auto *title = new QLabel(t("关闭这张贴图？"), this);
        QFont heading = body; heading.setPixelSize(15); heading.setWeight(QFont::DemiBold); title->setFont(heading);
        layout->addWidget(title); skip->setObjectName("PinCloseSkip"); layout->addWidget(skip);
        auto *actions = new QHBoxLayout; actions->setSpacing(8); actions->addStretch();
        auto *cancel = new QPushButton(t("取消"), this); cancel->setObjectName("PinCloseCancel"); cancel->setDefault(true);
        auto *close = new QPushButton(t("关闭贴图"), this); close->setObjectName("PinCloseConfirm");
        cancel->setFixedHeight(32); close->setFixedHeight(32);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        setStyleSheet(QStringLiteral("QPushButton { border: 1px solid transparent; border-radius: 7px; padding: 4px 14px; background: transparent; color: palette(text); }"
            "QPushButton:hover { background: palette(alternate-base); } QPushButton:focus { border-color: %1; }"
            "QPushButton#PinCloseConfirm { background: %2; color: white; }"
            "QPushButton#PinCloseConfirm:hover { background: %3; }"
            "QPushButton#PinCloseConfirm:pressed { background: %4; } QCheckBox { spacing: 8px; }")
            .arg(dark ? "#a8a8a8" : "#454b57", dark ? "#343434" : "#454b57",
                 dark ? "#404040" : "#353b46", dark ? "#242424" : "#272d37"));
        actions->addWidget(cancel); actions->addWidget(close); layout->addLayout(actions);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        setFixedWidth(300); adjustSize();
        auto *screen = QGuiApplication::screenAt(parent->geometry().center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        const auto available = screen->availableGeometry();
        const int below = parent->geometry().bottom() + 8;
        move(qBound(available.left() + 8, parent->geometry().center().x() - width() / 2, available.right() - width() - 7),
             qBound(available.top() + 8, below + height() <= available.bottom() ? below : parent->y() - height() - 8, available.bottom() - height() - 7));
    }
    QCheckBox *skip;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(palette().color(QPalette::Base));
        painter.setPen(palette().color(QPalette::Window).lightness() < 128 ? QColor("#383838") : QColor("#d9dde5"));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    }
};
} // namespace

Pin::Pin(const QImage &image, const QRect &geometry, const QJsonObject &saveOptions, const QString &language)
    : PinWindow(), image_(image), saveOptions_(saveOptions), language_(language) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(40, 50);
    setWindowTitle(t("拾影 · 贴图"));
    setAccessibleName(t("可选择文字的贴图，Alt 拖动移动窗口"));
    image_.setDevicePixelRatio(1);
    QScreen *screen = QGuiApplication::screenAt(geometry.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();
    QSize size = geometry.size().isEmpty() ? image.size() / screen->devicePixelRatio() : geometry.size();
    size.scale(available.size() * 0.85, Qt::KeepAspectRatio);
    const qreal initialScale = qMin(1.0, qMin(qreal(size.width()) / qMax(1, geometry.width()), qreal(size.height()) / qMax(1, geometry.height())));
    if (!geometry.size().isEmpty()) size = geometry.size() * initialScale;
    originalSize_ = QSize(qMax(40, size.width() + 6), qMax(50, size.height() + 28));
    resize(originalSize_);
    move(qBound(available.left(), geometry.left(), qMax(available.left(), available.right() - width() + 1)),
         qBound(available.top(), geometry.top(), qMax(available.top(), available.bottom() - height() + 1)));
    QTimer::singleShot(0, this, &Pin::recognize);
}

void Pin::configure(bool wheelZoom, bool hideOnBlur, bool confirmDoubleClickClose) {
    wheelZoom_ = wheelZoom; setHideOnBlur(hideOnBlur); confirmDoubleClickClose_ = confirmDoubleClickClose;
}

PinState Pin::state() const {
    auto result = layoutState(); result.image = image_; return result;
}

QRectF Pin::imageRect() const {
    const QRectF area = QRectF(rect()).adjusted(3, 3, -3, -25);
    const QSizeF size = QSizeF(image_.size()).scaled(area.size(), Qt::KeepAspectRatio);
    return QRectF(area.center() - QPointF(size.width() / 2, size.height() / 2), size);
}

QPointF Pin::imagePoint(const QPointF &point) const {
    const QRectF target = imageRect();
    return QPointF((point.x() - target.x()) * image_.width() / target.width(), (point.y() - target.y()) * image_.height() / target.height());
}

void Pin::setImage(const QImage &image) {
    if (image.isNull()) return;
    image_ = image;
    image_.setDevicePixelRatio(1);
    recognize();
    update();
}

void Pin::recognize() {
    const int generation = ++generation_;
    ocr_ = {};
    anchor_ = cursor_ = -1;
    allTextSelected_ = false;
    status_ = t("识别文字中… · Alt 拖动移动");
    update();
    delete ocrJob_.data();
    auto *job = new OcrJob(image_, language_, this);
    ocrJob_ = job;
    connect(job, &OcrJob::finished, this, [this, generation](const OcrResult &result) {
        if (generation != generation_) return; // Rotation or editing invalidates the previous image's OCR geometry.
        ocr_ = result;
        status_ = !result.error.isEmpty() ? result.error : ocr_.words.isEmpty() ? t("未识别到文字 · 拖动移动") : t("拖选文字 · Ctrl/Cmd+C 复制 · Alt 拖动移动");
        setToolTip(status_);
        update();
    });
}

void Pin::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF target = imageRect();
    painter.drawImage(target, image_);
    if (anchor_ >= 0 && cursor_ >= 0) {
        painter.save();
        painter.translate(target.topLeft());
        painter.scale(target.width() / image_.width(), target.height() / image_.height());
        for (int i = qMin(anchor_, cursor_); i <= qMax(anchor_, cursor_); ++i) painter.fillRect(ocr_.words[i].bounds, QColor(50, 140, 255, 90));
        painter.restore();
    }
    painter.setPen(isActiveWindow() ? QColor("#63b3ff") : QColor("#7c8594"));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(QRect(7, height() - 23, width() - 14, 20), Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(status_, Qt::ElideRight, width() - 14));
}

void Pin::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    allTextSelected_ = false;
    const int hit = event->modifiers() & Qt::AltModifier ? -1 : wordAt(ocr_.words, imagePoint(event->position()));
    if (hit >= 0) {
        selecting_ = true;
        anchor_ = cursor_ = hit;
    } else {
        anchor_ = cursor_ = -1;
        moving_ = true;
        dragOffset_ = event->globalPosition().toPoint() - pos();
    }
    update();
}

void Pin::mouseMoveEvent(QMouseEvent *event) {
    if (moving_) { move(event->globalPosition().toPoint() - dragOffset_); return; }
    const QPointF point = imagePoint(event->position());
    if (selecting_) {
        const int nearest = wordAt(ocr_.words, point, true);
        if (nearest >= 0) cursor_ = nearest;
        update();
    } else setCursor(wordAt(ocr_.words, point) >= 0 && !(event->modifiers() & Qt::AltModifier) ? Qt::IBeamCursor : Qt::ArrowCursor);
}

void Pin::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) { moving_ = false; selecting_ = false; }
}

void Pin::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || confirmingClose_) return;
    moving_ = selecting_ = false;
    if (confirmDoubleClickClose_) {
        confirmingClose_ = true;
        auto *prompt = new PinClosePrompt(this);
        connect(prompt, &QObject::destroyed, this, [this] {
            // Popup focus events can arrive after its deferred deletion.
            QTimer::singleShot(0, this, [this] { confirmingClose_ = false; });
        });
        // A local nonmodal popup must not activate the application's main-window group.
        connect(prompt, &QDialog::finished, this, [this, skip = prompt->skip](int result) {
            if (result != QDialog::Accepted) return;
            if (skip->isChecked()) emit doubleClickClosePromptDisabled();
            close();
        });
        prompt->show();
        return;
    }
    close();
}

void Pin::zoom(qreal factor, const QPoint &anchor) {
    const QSize before = size();
    QSize after = before * factor;
    if (after.width() < 40 || after.height() < 50 || after.width() > 8192 || after.height() > 8192) return;
    const QPointF fraction(qreal(anchor.x() - x()) / before.width(), qreal(anchor.y() - y()) / before.height());
    resize(after);
    move(anchor - QPoint(qRound(fraction.x() * width()), qRound(fraction.y() * height())));
}

void Pin::wheelEvent(QWheelEvent *event) {
    if (event->angleDelta().y() == 0) return;
    if (event->modifiers() & Qt::ControlModifier)
        setWindowOpacity(qBound(0.15, windowOpacity() + (event->angleDelta().y() > 0 ? 0.05 : -0.05), 1.0));
    else if (wheelZoom_) zoom(event->angleDelta().y() > 0 ? 1.1 : 1 / 1.1, event->globalPosition().toPoint());
    event->accept();
}

void Pin::copy(bool forceImage) {
    const QString text = forceImage ? QString() : allTextSelected_ ? ocr_.text : selectedText(ocr_, anchor_, cursor_);
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
    else {
        QString error;
        const bool copied = copyImageToClipboard(image_, saveOptions_, &error);
        status_ = !copied ? t("复制失败：") + error : saveOptions_.value("copyAsFile").toBool() ? t("已复制图像文件") : t("已复制图片");
        setToolTip(status_); update();
    }
}

void Pin::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Copy)) { copy(); return; }
    if (event->matches(QKeySequence::SelectAll) && !ocr_.text.isEmpty()) { allTextSelected_ = true; anchor_ = 0; cursor_ = int(ocr_.words.size()) - 1; update(); return; }
    if (QKeySequence(event->keyCombination()) == QKeySequence("Ctrl+Shift+S")) { save(true); return; }
    if (event->matches(QKeySequence::Save)) { save(); return; }
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Delete) { close(); return; }
    if (event->key() == Qt::Key_Space) { emit editRequested(this); return; }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_O) { recognize(); return; }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_R) { setImage(image_.transformed(QTransform().rotate(90))); return; }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) { zoom(1.1, geometry().center()); return; }
    if (event->key() == Qt::Key_Minus) { zoom(1 / 1.1, geometry().center()); return; }
    QWidget::keyPressEvent(event);
}

void Pin::contextMenuEvent(QContextMenuEvent *event) {
    // Closing all pins can destroy this menu before exec() returns.
    QPointer<QMenu> menu = new QMenu(this);
    const auto cleanup = qScopeGuard([&] { delete menu.data(); });
    menu->setObjectName("PinMenu");
    const bool textSelected = allTextSelected_ || !selectedText(ocr_, anchor_, cursor_).isEmpty();
    menu->addAction(textSelected ? t("复制选中文字") : t("复制图片"), this, [this] { copy(); });
    menu->addAction(t("保存图片…"), this, [this] { save(); });
    menu->addAction(t("编辑标注…"), this, [this] { emit editRequested(this); });
    auto *more = menu->addMenu(t("更多操作"));
    more->setObjectName("PinMoreMenu");
    if (textSelected) more->addAction(t("复制图片"), this, [this] { copy(true); });
    auto *allText = more->addAction(t("复制全部文字"), this, [this] {
        QApplication::clipboard()->setText(ocr_.text);
    });
    allText->setEnabled(!ocr_.text.isEmpty());
    auto *proofread = more->addAction(t("校对识别文字…"), this, [this] { (new OcrResultDialog(ocr_.text, this))->show(); });
    proofread->setObjectName("ProofreadOcrText"); proofread->setEnabled(!ocr_.text.isEmpty());
    more->addAction(t("重新识别文字"), this, &Pin::recognize);
    more->addSeparator();
    more->addAction(t("快速保存"), this, [this] { save(true); });
    more->addAction(t("旋转 90°"), this, [this] { setImage(image_.transformed(QTransform().rotate(90))); });
    more->addAction(t("恢复大小和透明度"), this, [this] { resize(originalSize_); setWindowOpacity(1); });
    auto *top = more->addAction(t("保持置顶"));
    top->setCheckable(true);
    top->setChecked(windowFlags() & Qt::WindowStaysOnTopHint);
    connect(top, &QAction::triggered, this, [this](bool on) { setWindowFlag(Qt::WindowStaysOnTopHint, on); show(); });
    more->addSeparator();
    more->addAction(t("隐藏/显示全部贴图"), this, &Pin::toggleAllRequested);
    auto *restore = more->addAction(t("恢复最近关闭贴图"), this, &Pin::restoreClosedRequested);
    restore->setObjectName("RestoreClosedPin");
    restore->setEnabled(canRestoreClosed_);
    more->addAction(t("关闭全部贴图"), this, &Pin::closeAllRequested);
    menu->addSeparator();
    menu->addAction(t("关闭"), this, &QWidget::close);
    menu->exec(event->globalPos());
}

void Pin::save(bool quick) {
    if (!quick) { if (saveWithDialog(this, image_, saveOptions_)) emit imageSaved(image_); return; }
    QString error;
    const QString path = exportImage(image_, saveOptions_, &error);
    status_ = path.isEmpty() ? t("保存失败") + ": " + error : t("已保存到：") + path;
    update();
    if (!path.isEmpty()) emit imageSaved(image_);
}


} // namespace still
