#include "ui/i18n.h"
#include "editor.h"
#include "ocr.h"
#include "storage/store.h"
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
#include <QMouseEvent>
#include <QPainter>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

namespace pixclone {

bool saveWithDialog(QWidget *parent, const QImage &image, const QString &directory) {
    const QString folder = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : directory;
    QFileDialog dialog(parent, t("保存截图"), uniqueImagePath(folder));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({t("PNG 图像 (*.png)"), t("JPEG 图像 (*.jpg *.jpeg)")});
    // Resolve the suffix inside the dialog, so its overwrite confirmation checks the actual target.
    dialog.setDefaultSuffix("png");
    QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString &filter) {
        dialog.setDefaultSuffix(filter.startsWith("JPEG") ? "jpg" : "png");
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    const QString path = dialog.selectedFiles().first();
    QString error;
    if (!saveImage(image, path, &error)) { QMessageBox::warning(parent, t("保存失败"), error); return false; }
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
    if (drawing_) {
        QImage preview = cached_;
        paintAnnotation(preview, pending_);
        painter.drawImage(imageRect(), preview);
    } else painter.drawImage(imageRect(), cached_);
}

void Canvas::refresh() { cached_ = document.render(); update(); emit changed(); }
void Canvas::undo() { document.undo(); refresh(); }
void Canvas::redo() { document.redo(); refresh(); }
void Canvas::removeLast() { undo(); }

void Canvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !imageRect().contains(event->position())) return;
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
    if (!drawing_) return;
    if (tool == Tool::Pen || tool == Tool::Highlight || tool == Tool::MosaicPen) pending_.points.append(imagePoint(event->position()));
    else {
        pending_.points.resize(2);
        pending_.points[1] = imagePoint(event->position());
    }
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if (!drawing_ || event->button() != Qt::LeftButton) return;
    mouseMoveEvent(event);
    drawing_ = false;
    document.add(pending_);
    refresh();
}

Editor::Editor(const QImage &image, const QString &saveDirectory, const QString &language, QWidget *parent)
    : QMainWindow(parent), canvas_(new Canvas(image, this)), saveDirectory_(saveDirectory), language_(language) {
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
        connect(action, &QAction::triggered, this, [this, i] { canvas_->tool = Tool(i); });
    }
    tools->addSeparator();
    auto *color = tools->addAction(toolIcon(ToolIcon::Color, canvas_->color), t("颜色"));
    connect(color, &QAction::triggered, this, [this, color] {
        const QColor chosen = QColorDialog::getColor(canvas_->color, this, t("标注颜色"));
        if (chosen.isValid()) { canvas_->color = chosen; color->setIcon(toolIcon(ToolIcon::Color, chosen)); }
    });
    auto *width = new QSpinBox(tools);
    width->setRange(1, 30);
    width->setValue(3);
    width->setSuffix(" px");
    width->setFixedWidth(76);
    width->setButtonSymbols(QAbstractSpinBox::NoButtons);
    width->setToolTip(t("线宽（像素），可输入数字或滚轮调整"));
    width->setAccessibleName(t("标注线宽"));
    tools->addWidget(width);
    connect(width, &QSpinBox::valueChanged, this, [this](int value) { canvas_->lineWidth = value; });
    tools->addSeparator();
    auto *undo = tools->addAction(toolIcon(ToolIcon::Undo), t("撤销"));
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, canvas_, &Canvas::undo);
    auto *redo = tools->addAction(toolIcon(ToolIcon::Redo), t("重做"));
    redo->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    connect(redo, &QAction::triggered, canvas_, &Canvas::redo);
    auto *remove = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    connect(remove, &QShortcut::activated, canvas_, &Canvas::removeLast);
    tools->addSeparator();
    auto *copy = tools->addAction(toolIcon(ToolIcon::Copy), t("复制图片"));
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, [this] {
        const QImage image = canvas_->rendered();
        QApplication::clipboard()->setImage(image);
        emit imageCopied(image);
        statusBar()->showMessage(t("已复制图片"), 2000);
    });
    auto *saveAction = tools->addAction(toolIcon(ToolIcon::Save), t("保存"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &Editor::save);
    auto *pin = tools->addAction(toolIcon(ToolIcon::Pin), t("贴图"));
    pin->setShortcut(QKeySequence("Ctrl+P"));
    connect(pin, &QAction::triggered, this, [this] { emit pinRequested(canvas_->rendered()); close(); });
    auto *ocr = tools->addAction(toolIcon(ToolIcon::Ocr), t("复制文字"));
    ocr->setShortcut(QKeySequence("Ctrl+O"));
    connect(ocr, &QAction::triggered, this, &Editor::recognize);
    auto *apply = tools->addAction(toolIcon(ToolIcon::Apply), t("应用并关闭"));
    connect(apply, &QAction::triggered, this, [this] { emit applyRequested(canvas_->rendered()); close(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);
    connect(canvas_, &Canvas::changed, this, [this, undo, redo] {
        undo->setEnabled(canvas_->document.canUndo());
        redo->setEnabled(canvas_->document.canRedo());
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
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
    statusBar()->showMessage(t("%1 × %2 像素 · 拖动绘制标注 · 贴图后可选择文字").arg(image.width()).arg(image.height()));
}

void Editor::save() {
    const auto image = canvas_->rendered();
    if (saveWithDialog(this, image, saveDirectory_)) { emit imageSaved(image); statusBar()->showMessage(t("已保存"), 2000); }
}

void Editor::recognize() {
    statusBar()->showMessage(t("正在识别文字…"));
    delete ocrJob_.data();
    auto *job = new OcrJob(canvas_->rendered(), language_, this);
    ocrJob_ = job;
    connect(job, &OcrJob::finished, this, [this](const OcrResult &result) {
        if (!result.error.isEmpty()) { statusBar()->showMessage(result.error); return; }
        const QString text = selectedText(result.words, 0, int(result.words.size()) - 1);
        if (text.isEmpty()) { statusBar()->showMessage(t("未识别到文字")); return; }
        QApplication::clipboard()->setText(text);
        statusBar()->showMessage(t("已复制识别文字"), 3000);
    });
}

} // namespace pixclone
