#pragma once

#include "pin_window.h"

class QTextEdit;

namespace still {

class TextPin : public PinWindow {
    Q_OBJECT
public:
    explicit TextPin(QString text, QString html = {});
    PinState state() const override;
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
private:
    QString text_;
    QString html_;
    QVector<qsizetype> sourceOffsets_;
    QTextEdit *editor_;
    bool moving_ = false;
    QPoint dragOffset_;
    void copy();
    void showMenu(const QPoint &globalPosition);
};

} // namespace still
