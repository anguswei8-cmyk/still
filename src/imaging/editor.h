#pragma once

#include "image.h"
#include <QMainWindow>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>

namespace still {

class OcrJob;

class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QImage image, QWidget *parent = nullptr);
    ImageDocument document;
    Tool tool = Tool::Rectangle;
    QColor color = QColor("#ff4c5b");
    qreal lineWidth = 3;
    QImage rendered() const { return document.render(); }
    QRectF imageRect() const;
    void undo();
    void redo();
    void removeLast();
    void setTool(Tool value);
    void setSelectionMode(bool enabled);
    bool selectionMode() const { return mode_ == Mode::Select; }
    void setCropMode(bool enabled);
    bool cropMode() const { return mode_ == Mode::Crop; }
    QRect cropSelection() const { return crop_; }
    bool applyCrop();
    void flip(Qt::Orientation orientation);
    void applyEffect(ImageEffect effect, int amount = 0);
    void previewBrightness(std::optional<int> amount);
    int selectedAnnotation() const { return selected_; }
    void setColor(const QColor &value);
    void setLineWidth(qreal value);
    void editSelectedText();
    bool cancelInteraction();
signals:
    void changed();
    void selectionChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QPointF imagePoint(QPointF point) const;
    Annotation pending_;
    bool drawing_ = false;
    enum class Mode { Draw, Select, Crop };
    Mode mode_ = Mode::Draw;
    bool moving_ = false;
    bool cropping_ = false;
    QPointF cropAnchor_;
    QRect crop_;
    int selected_ = -1;
    QPointF pressPoint_;
    QImage cached_;
    std::optional<int> brightnessPreview_;
    void refresh();
    void select(int index);
};

class Editor : public QMainWindow {
    Q_OBJECT
public:
    explicit Editor(const QImage &image, const QJsonObject &saveOptions, const QString &language, bool editingPin = false, QWidget *parent = nullptr);
    void configureSave(const QJsonObject &options) { saveOptions_ = options; }
signals:
    void pinRequested(const QImage &image);
    void imageCopied(const QImage &image);
    void imageSaved(const QImage &image);
    void applyRequested(const QImage &image);
private:
    Canvas *canvas_;
    QJsonObject saveOptions_;
    QString language_;
    QPointer<OcrJob> ocrJob_;
    quint64 ocrRevision_ = 0;
    void save();
    void quickSave();
    void recognize();
    void adjustBrightness();
};

bool saveWithDialog(QWidget *parent, const QImage &image, const QJsonObject &options);

} // namespace still
