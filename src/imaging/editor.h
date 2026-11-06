#pragma once

#include "image.h"
#include <QMainWindow>
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
signals:
    void changed();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QPointF imagePoint(QPointF point) const;
    Annotation pending_;
    bool drawing_ = false;
    QImage cached_;
    void refresh();
};

class Editor : public QMainWindow {
    Q_OBJECT
public:
    explicit Editor(const QImage &image, const QString &saveDirectory, const QString &language, QWidget *parent = nullptr);
signals:
    void pinRequested(const QImage &image);
    void imageCopied(const QImage &image);
    void imageSaved(const QImage &image);
    void applyRequested(const QImage &image);
private:
    Canvas *canvas_;
    QString saveDirectory_;
    QString language_;
    QPointer<OcrJob> ocrJob_;
    void save();
    void recognize();
};

bool saveWithDialog(QWidget *parent, const QImage &image, const QString &directory);

} // namespace still
