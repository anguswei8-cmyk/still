#include "ui/i18n.h"
#include "editor.h"
#include "ocr.h"
#include "ocr_result_dialog.h"
#include "storage/store.h"
#include "storage/image_export.h"
#include "clipboard/image_copy.h"
#include "ui/toolicons.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace still {

bool saveWithDialog(QWidget *parent, const QImage &image, const QJsonObject &options) {
    QFileDialog dialog(parent, t("保存截图"), uniqueImagePath(imageExportDirectory(options), imageExportFileName(options)));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({t("PNG 图像 (*.png)"), t("JPEG 图像 (*.jpg *.jpeg)")});
    // Resolve the suffix inside the dialog, so its overwrite confirmation checks the actual target.
    const bool jpeg = options.value("format").toString() == "jpeg";
    dialog.selectNameFilter(jpeg ? t("JPEG 图像 (*.jpg *.jpeg)") : t("PNG 图像 (*.png)"));
    dialog.setDefaultSuffix(jpeg ? "jpg" : "png");
    QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString &filter) {
        const QString suffix = filter.startsWith("JPEG") ? "jpg" : "png";
        dialog.setDefaultSuffix(suffix);
        const auto files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            const QFileInfo file(files.first());
            if (!file.isDir()) {
                const QString extension = file.suffix().toLower();
                const QString stem = extension == "png" || extension == "jpg" || extension == "jpeg" ? file.completeBaseName() : file.fileName();
                dialog.selectFile(stem + '.' + suffix);
            }
        }
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    const QString path = dialog.selectedFiles().first();
    QString error;
    if (!saveImage(image, path, &error, -1, options.value("jpegQuality").toInt(95), options.value("pngCompression").toInt(50))) { QMessageBox::warning(parent, t("保存失败"), error); return false; }
    return true;
}

Canvas::Canvas(QImage image, QWidget *parent) : QWidget(parent), document(std::move(image)) {
    setMinimumSize(160, 100);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(t("截图标注画布"));
    cached_ = document.render();
}

QRectF Canvas::imageRect() const {
    if (cached_.isNull()) return {};
    const QSizeF available(qMax(1, width() - 32), qMax(1, height() - 32));
    const QSizeF size = QSizeF(cached_.size()).scaled(available, Qt::KeepAspectRatio);
    return QRectF((width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height());
}

QPointF Canvas::imagePoint(QPointF point) const {
    const QRectF view = imageRect();
    return QPointF(qBound(0.0, (point.x() - view.left()) * cached_.width() / view.width(), qreal(cached_.width())),
                   qBound(0.0, (point.y() - view.top()) * cached_.height() / view.height(), qreal(cached_.height())));
}

void Canvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QImage preview = cached_;
    if (brightnessPreview_) preview = document.previewBrightness(*brightnessPreview_);
    else if (moving_) preview = document.render(selected_, pending_);
    else if (drawing_) paintAnnotation(preview, pending_);
    const auto view = imageRect(); painter.drawImage(view, preview);
    if (cropMode() && !crop_.isEmpty()) {
        const qreal scale = view.width() / cached_.width();
        const QRectF area(view.topLeft() + QPointF(crop_.topLeft()) * scale, QSizeF(crop_.size()) * scale);
        QPainterPath shade; shade.addRect(view); shade.addRect(area);
        painter.fillPath(shade, QColor(0, 0, 0, 140));
        painter.setBrush(Qt::NoBrush); painter.setPen(QPen(Qt::white, 2)); painter.drawRect(area);
        painter.setPen(QPen(QColor("#454b57"), 1, Qt::DashLine)); painter.drawRect(area);
    }
    if (selected_ >= 0) {
        const auto bounds = moving_ ? document.bounds(selected_, pending_) : document.bounds(selected_);
        const qreal scale = view.width() / cached_.width();
        const QRectF outline(view.topLeft() + bounds.topLeft() * scale, bounds.size() * scale);
        painter.setClipRect(view); painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 3)); painter.drawRect(outline.adjusted(-3, -3, 3, 3));
        painter.setPen(QPen(QColor("#454b57"), 1, Qt::DashLine)); painter.drawRect(outline.adjusted(-3, -3, 3, 3));
    }
}

void Canvas::refresh() { cached_ = document.render(); update(); emit changed(); }
void Canvas::select(int index) {
    selected_ = index;
    if (selected_ >= 0) { color = document.annotations()[selected_].color; lineWidth = document.annotations()[selected_].width; }
    update(); emit selectionChanged();
}
bool Canvas::cancelInteraction() {
    if (!drawing_ && !moving_ && selected_ < 0 && !cropMode()) return false;
    drawing_ = false; moving_ = false; cropping_ = false; crop_ = {};
    if (cropMode()) { mode_ = Mode::Draw; unsetCursor(); }
    select(-1); return true;
}
void Canvas::setTool(Tool value) { cancelInteraction(); mode_ = Mode::Draw; tool = value; setFocus(); emit selectionChanged(); }
void Canvas::setSelectionMode(bool enabled) { cancelInteraction(); mode_ = enabled ? Mode::Select : Mode::Draw; setFocus(); emit selectionChanged(); }
void Canvas::setCropMode(bool enabled) {
    cancelInteraction(); mode_ = enabled ? Mode::Crop : Mode::Draw;
    if (enabled) setCursor(Qt::CrossCursor); else unsetCursor();
    setFocus(); emit selectionChanged();
}
bool Canvas::applyCrop() {
    if (!cropMode() || crop_.isEmpty()) return false;
    const bool applied = document.crop(crop_);
    cancelInteraction(); refresh(); return applied;
}
void Canvas::flip(Qt::Orientation orientation) { cancelInteraction(); select(-1); document.flip(orientation); refresh(); setFocus(); }
void Canvas::applyEffect(ImageEffect effect, int amount) {
    if (effect == ImageEffect::Brightness && amount == 0) return;
    cancelInteraction(); select(-1); document.applyEffect(effect, amount); refresh(); setFocus();
}
void Canvas::previewBrightness(std::optional<int> amount) { brightnessPreview_ = amount; update(); }
void Canvas::undo() {
    if (cropMode()) { cancelInteraction(); return; }
    if (drawing_ || moving_) { drawing_ = false; moving_ = false; update(); return; }
    select(-1); document.undo(); refresh();
}
void Canvas::redo() {
    cancelInteraction(); select(-1); document.redo(); refresh();
}
void Canvas::removeLast() {
    if (selected_ >= 0) { document.remove(selected_); select(-1); moving_ = false; refresh(); }
    else if (!selectionMode()) undo();
}
void Canvas::setColor(const QColor &value) {
    color = value;
    if (selected_ >= 0) { auto annotation = document.annotations()[selected_]; annotation.color = value; document.replace(selected_, annotation); refresh(); }
    emit selectionChanged();
}
void Canvas::setLineWidth(qreal value) {
    lineWidth = value;
    if (selected_ >= 0) { auto annotation = document.annotations()[selected_]; annotation.width = value; document.replace(selected_, annotation); refresh(); }
    emit selectionChanged();
}
void Canvas::editSelectedText() {
    if (selected_ < 0) return;
    auto annotation = document.annotations()[selected_];
    if (annotation.tool != Tool::Text && annotation.tool != Tool::Number) return;
    moving_ = false;
    bool accepted = false;
    const auto text = QInputDialog::getMultiLineText(this, t("修改标注文字"), t("输入文字："), annotation.text, &accepted);
    if (!accepted) { update(); return; }
    if (text.isEmpty()) { removeLast(); return; }
    annotation.text = text; document.replace(selected_, annotation); refresh();
}
void Canvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !imageRect().contains(event->position())) return;
    setFocus();
    if (cropMode()) {
        cropAnchor_ = imagePoint(event->position()); crop_ = {}; cropping_ = true; update(); emit selectionChanged(); return;
    }
    if (selectionMode()) {
        select(document.hitTest(imagePoint(event->position()), 5 * cached_.width() / imageRect().width()));
        if (selected_ >= 0) { pending_ = document.annotations()[selected_]; pressPoint_ = imagePoint(event->position()); moving_ = true; }
        return;
    }
    pending_ = {tool, {imagePoint(event->position())}, color, lineWidth, {}};
    if (tool == Tool::Text) {
        bool accepted = false;
        pending_.text = QInputDialog::getMultiLineText(this, t("文字标注"), t("输入文字："), {}, &accepted);
        if (accepted && !pending_.text.isEmpty()) { document.add(pending_); refresh(); }
        return;
    }
    if (tool == Tool::Number) {
        pending_.text = QString::number(document.nextNumber());
        document.add(pending_);
        refresh();
        return;
    }
    drawing_ = true;
    update();
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
    if (cropping_) {
        crop_ = pixelRect(QRectF(cropAnchor_, imagePoint(event->position())), cached_.size(), cached_.size());
        update(); emit selectionChanged(); return;
    }
    if (moving_) { pending_ = document.translated(selected_, imagePoint(event->position()) - pressPoint_); update(); return; }
    if (!drawing_) return;
    if (tool == Tool::Pen || tool == Tool::Highlight || tool == Tool::MosaicPen) pending_.points.append(imagePoint(event->position()));
    else {
        pending_.points.resize(2);
        pending_.points[1] = imagePoint(event->position());
    }
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (cropping_ && event->button() == Qt::LeftButton) { mouseMoveEvent(event); cropping_ = false; return; }
    if (moving_ && event->button() == Qt::LeftButton) {
        mouseMoveEvent(event); moving_ = false; document.replace(selected_, pending_); refresh(); return;
    }
    if (!drawing_ || event->button() != Qt::LeftButton) return;
    mouseMoveEvent(event);
    drawing_ = false;
    document.add(pending_);
    refresh();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if (selectionMode() && event->button() == Qt::LeftButton && imageRect().contains(event->position())) {
        moving_ = false; select(document.hitTest(imagePoint(event->position()), 5 * cached_.width() / imageRect().width())); editSelectedText(); return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
void Canvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && cancelInteraction()) return;
    if (cropMode()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) applyCrop();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) { removeLast(); return; }
    if (event->key() == Qt::Key_F2) { editSelectedText(); return; }
    if (selected_ >= 0) {
        QPointF delta;
        if (event->key() == Qt::Key_Left) delta.setX(-1);
        else if (event->key() == Qt::Key_Right) delta.setX(1);
        else if (event->key() == Qt::Key_Up) delta.setY(-1);
        else if (event->key() == Qt::Key_Down) delta.setY(1);
        if (!delta.isNull()) {
            if (event->modifiers() & Qt::ShiftModifier) delta *= 10;
            moving_ = false; document.replace(selected_, document.translated(selected_, delta)); refresh(); return;
        }
    }
    QWidget::keyPressEvent(event);
}

Editor::Editor(const QImage &image, const QJsonObject &saveOptions, const QString &language, bool editingPin, QWidget *parent)
    : QMainWindow(parent), canvas_(new Canvas(image, this)), saveOptions_(saveOptions), language_(language) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(t("拾影 · 编辑截图"));
    resize(1100, 760);
    setCentralWidget(canvas_);
    auto *tools = addToolBar(t("标注工具"));
    tools->setObjectName("CaptureTools");
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tools->setIconSize(QSize(20, 20));
    tools->setStyleSheet(toolbarStyleSheet());
    auto *toolGroup = new QActionGroup(tools);
    toolGroup->setExclusive(true);
    auto *select = tools->addAction(toolIcon(ToolIcon::Select), t("选择标注")); select->setObjectName("selectAnnotation");
    select->setCheckable(true); select->setShortcut(QKeySequence("V")); toolGroup->addAction(select);
    connect(select, &QAction::triggered, this, [this] { canvas_->setSelectionMode(true); });
    auto *crop = tools->addAction(toolIcon(ToolIcon::Crop), t("裁切图片")); crop->setObjectName("cropImage");
    crop->setCheckable(true); crop->setShortcut(QKeySequence("C")); toolGroup->addAction(crop);
    connect(crop, &QAction::triggered, this, [this] {
        if (canvas_->cropMode() && !canvas_->cropSelection().isEmpty()) canvas_->applyCrop(); else canvas_->setCropMode(true);
    });
    auto *imageMenu = new QMenu(tools); imageMenu->setObjectName("ImageTransformMenu");
    auto *horizontal = imageMenu->addAction(toolIcon(ToolIcon::FlipHorizontal), t("水平翻转")); horizontal->setObjectName("flipHorizontal");
    auto *vertical = imageMenu->addAction(toolIcon(ToolIcon::FlipVertical), t("垂直翻转")); vertical->setObjectName("flipVertical");
    connect(horizontal, &QAction::triggered, this, [this] { canvas_->flip(Qt::Horizontal); });
    connect(vertical, &QAction::triggered, this, [this] { canvas_->flip(Qt::Vertical); });
    imageMenu->addSeparator();
    auto *grayscale = imageMenu->addAction(t("灰度")); grayscale->setObjectName("grayscaleImage");
    auto *invert = imageMenu->addAction(t("反色")); invert->setObjectName("invertImage");
    connect(grayscale, &QAction::triggered, this, [this] { canvas_->applyEffect(ImageEffect::Grayscale); });
    connect(invert, &QAction::triggered, this, [this] { canvas_->applyEffect(ImageEffect::Invert); });
    auto *brightness = imageMenu->addAction(t("调整亮度…")); brightness->setObjectName("brightnessImage");
    connect(brightness, &QAction::triggered, this, &Editor::adjustBrightness);
    auto *cropButton = qobject_cast<QToolButton *>(tools->widgetForAction(crop));
    cropButton->setObjectName("ImageTransforms"); cropButton->setPopupMode(QToolButton::MenuButtonPopup); cropButton->setMenu(imageMenu);
    QVector<QAction *> drawingActions;
    const QString keys = "RGOLAPHMBTD";
    const QStringList names{t("矩形"), t("圆角矩形"), t("椭圆"), t("直线"),
        t("箭头"), t("画笔"), t("荧光笔"), t("马赛克"),
        t("马赛克笔"), t("文字"), t("序号")};
    const QVector<ToolIcon> icons{ToolIcon::Rectangle, ToolIcon::RoundedRectangle, ToolIcon::Ellipse, ToolIcon::Line,
        ToolIcon::Arrow, ToolIcon::Pen, ToolIcon::Highlight, ToolIcon::Mosaic, ToolIcon::MosaicPen, ToolIcon::Text, ToolIcon::Number};
    for (int i = 0; i < keys.size(); ++i) {
        auto *action = tools->addAction(toolIcon(icons[i]), names[i]);
        action->setObjectName("annotationTool-" + QString::number(i));
        action->setCheckable(true);
        action->setChecked(i == 0);
        action->setShortcut(QKeySequence(keys.mid(i, 1)));
        toolGroup->addAction(action);
        drawingActions.append(action);
        connect(action, &QAction::triggered, this, [this, i] { canvas_->setTool(Tool(i)); });
    }
    tools->addSeparator();
    auto *color = tools->addAction(toolIcon(ToolIcon::Color, canvas_->color), t("颜色"));
    color->setObjectName("annotationColor");
    connect(color, &QAction::triggered, this, [this] {
        const QColor chosen = QColorDialog::getColor(canvas_->color, this, t("标注颜色"));
        if (chosen.isValid()) canvas_->setColor(chosen);
    });
    auto *width = new QSpinBox(tools);
    width->setRange(1, 30);
    width->setValue(3);
    width->setObjectName("annotationWidth"); width->setKeyboardTracking(false);
    width->setSuffix(" px");
    width->setFixedWidth(76);
    width->setToolTip(t("线宽（像素），可输入数字或滚轮调整"));
    width->setAccessibleName(t("标注线宽"));
    tools->addWidget(width);
    connect(width, &QSpinBox::valueChanged, this, [this](int value) { canvas_->setLineWidth(value); });
    auto *editText = tools->addAction(toolIcon(ToolIcon::Edit), t("修改标注文字")); editText->setObjectName("annotationTextEdit"); editText->setShortcut(QKeySequence(Qt::Key_F2));
    editText->setEnabled(false); connect(editText, &QAction::triggered, canvas_, &Canvas::editSelectedText);
    tools->addSeparator();
    auto *undo = tools->addAction(toolIcon(ToolIcon::Undo), t("撤销"));
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, canvas_, &Canvas::undo);
    auto *redo = tools->addAction(toolIcon(ToolIcon::Redo), t("重做"));
    redo->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    connect(redo, &QAction::triggered, canvas_, &Canvas::redo);
    tools->addSeparator();
    auto *copy = tools->addAction(toolIcon(ToolIcon::Copy), t("复制图片"));
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, [this] {
        const QImage image = canvas_->rendered();
        QString error;
        if (!copyImageToClipboard(image, saveOptions_, &error)) { statusBar()->showMessage(t("复制失败：") + error); return; }
        emit imageCopied(image);
        statusBar()->showMessage(saveOptions_.value("copyAsFile").toBool() ? t("已复制图像文件") : t("已复制图片"), 2000);
    });
    auto *saveAction = tools->addAction(toolIcon(ToolIcon::Save), t("保存"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &Editor::save);
    auto *quick = tools->addAction(toolIcon(ToolIcon::QuickSave), t("快速保存"));
    quick->setObjectName("quickSave");
    quick->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(quick, &QAction::triggered, this, &Editor::quickSave);
    auto *pin = tools->addAction(toolIcon(ToolIcon::Pin), t("贴图"));
    pin->setShortcut(QKeySequence("Ctrl+P"));
    connect(pin, &QAction::triggered, this, [this] { emit pinRequested(canvas_->rendered()); close(); });
    auto *ocr = tools->addAction(toolIcon(ToolIcon::Ocr), t("复制文字"));
    ocr->setShortcut(QKeySequence("Ctrl+O"));
    connect(ocr, &QAction::triggered, this, &Editor::recognize);
    if (editingPin) {
        auto *apply = tools->addAction(toolIcon(ToolIcon::Apply), t("应用并关闭"));
        connect(apply, &QAction::triggered, this, [this] { emit applyRequested(canvas_->rendered()); close(); });
    }
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] { if (!canvas_->cancelInteraction()) close(); });
    connect(canvas_, &Canvas::changed, this, [this, undo, redo] {
        undo->setEnabled(canvas_->document.canUndo());
        redo->setEnabled(canvas_->document.canRedo());
        if (ocrJob_ && ocrRevision_ != canvas_->document.revision()) {
            delete ocrJob_.data(); statusBar()->showMessage(t("图像已更改，请重新识别文字"));
        }
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(canvas_, &Canvas::selectionChanged, this, [this, select, crop, cropButton, drawingActions, color, width, editText] {
        if (canvas_->cropMode()) crop->setChecked(true);
        else if (canvas_->selectionMode()) select->setChecked(true);
        else drawingActions[int(canvas_->tool)]->setChecked(true);
        const bool ready = canvas_->cropMode() && !canvas_->cropSelection().isEmpty();
        crop->setText(ready ? t("应用裁切") : t("裁切图片")); cropButton->setAccessibleName(crop->text());
        crop->setToolTip(ready ? t("应用裁切（Enter 或 C）") : t("裁切图片（C），下拉可处理图像"));
        color->setIcon(toolIcon(ToolIcon::Color, canvas_->color));
        const QSignalBlocker blocker(width); width->setValue(qRound(canvas_->lineWidth));
        const int index = canvas_->selectedAnnotation();
        const bool text = index >= 0 && (canvas_->document.annotations()[index].tool == Tool::Text || canvas_->document.annotations()[index].tool == Tool::Number);
        editText->setEnabled(text);
        color->setEnabled(!canvas_->cropMode() && (index < 0 || (canvas_->document.annotations()[index].tool != Tool::Mosaic && canvas_->document.annotations()[index].tool != Tool::MosaicPen)));
        width->setEnabled(!canvas_->cropMode() && (index < 0 || canvas_->document.annotations()[index].tool != Tool::Mosaic));
        if (canvas_->cropMode()) {
            const auto area = canvas_->cropSelection();
            statusBar()->showMessage(area.isEmpty() ? t("拖动选择裁切区域 · Enter 确认 · Esc 取消") : t("裁切 %1 × %2 像素 · Enter 确认 · Esc 取消").arg(area.width()).arg(area.height()));
        }
        else if (!canvas_->selectionMode()) statusBar()->showMessage(t("%1 × %2 像素 · 拖动绘制标注 · V 选择已有标注").arg(canvas_->document.size().width()).arg(canvas_->document.size().height()));
        else if (index < 0) statusBar()->showMessage(t("单击标注轮廓选择，拖动移动 · Esc 关闭"));
        else statusBar()->showMessage(text ? t("拖动移动 · 方向键微调 · Delete 删除 · F2 改字 · Esc 取消选择") : t("拖动移动 · 方向键微调 · Delete 删除 · Esc 取消选择"));
    });
    for (QAction *action : tools->actions()) {
        if (auto *button = qobject_cast<QToolButton *>(tools->widgetForAction(action))) {
            button->setAccessibleName(action->text());
            const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
            action->setToolTip(shortcut.isEmpty() ? action->text() : action->text() + " (" + shortcut + ')');
        }
    }
    // Qt's overflow button has no QAction; give it the same icon and labeling quality.
    for (auto *button : tools->findChildren<QToolButton *>()) if (!button->defaultAction()) {
        button->setIcon(toolIcon(ToolIcon::More));
        button->setAccessibleName(t("更多工具"));
        button->setToolTip(t("更多工具"));
    }
    crop->setToolTip(t("裁切图片（C），下拉可处理图像"));
    statusBar()->showMessage(t("%1 × %2 像素 · 拖动绘制标注 · V 选择已有标注").arg(image.width()).arg(image.height()));
}

void Editor::adjustBrightness() {
    QInputDialog dialog(this); dialog.setObjectName("BrightnessDialog");
    dialog.setWindowTitle(t("调整亮度"));
    dialog.setLabelText(t("负值变暗，正值变亮（-100 至 100）："));
    dialog.setInputMode(QInputDialog::IntInput); dialog.setIntRange(-100, 100); dialog.setIntStep(1); dialog.setIntValue(0);
    dialog.setOkButtonText(t("应用")); dialog.setCancelButtonText(t("取消"));
    // Native Cocoa buttons and step arrows do not honor this dialog's dark palette.
    auto *style = QStyleFactory::create("Fusion"); style->setParent(&dialog);
    dialog.setStyle(style);
    for (auto *widget : dialog.findChildren<QWidget *>()) widget->setStyle(style);
    connect(&dialog, &QInputDialog::intValueChanged, canvas_, [this](int value) { canvas_->previewBrightness(value); });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    canvas_->previewBrightness(std::nullopt);
    if (accepted) canvas_->applyEffect(ImageEffect::Brightness, dialog.intValue());
}

void Editor::save() {
    const auto image = canvas_->rendered();
    if (saveWithDialog(this, image, saveOptions_)) { emit imageSaved(image); statusBar()->showMessage(t("已保存"), 2000); }
}

void Editor::quickSave() {
    const auto image = canvas_->rendered();
    QString error;
    const QString path = exportImage(image, saveOptions_, &error);
    if (path.isEmpty()) { statusBar()->showMessage(t("保存失败") + ": " + error); return; }
    emit imageSaved(image);
    statusBar()->showMessage(t("已保存到：") + path);
}

void Editor::recognize() {
    statusBar()->showMessage(t("正在识别文字…"));
    delete ocrJob_.data();
    ocrRevision_ = canvas_->document.revision();
    auto *job = new OcrJob(canvas_->rendered(), language_, this);
    ocrJob_ = job;
    connect(job, &OcrJob::finished, this, [this](const OcrResult &result) {
        if (!result.error.isEmpty()) { statusBar()->showMessage(result.error); return; }
        const QString &text = result.text;
        if (text.isEmpty()) { statusBar()->showMessage(t("未识别到文字")); return; }
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage(t("已复制识别文字"), 3000);
        (new OcrResultDialog(text, this))->show();
    });
}

} // namespace still
