#include "pin_window.h"
#include <QApplication>
#include <QCloseEvent>
#include <QScreen>
#include <QTimer>
#include <QUuid>

namespace still {

PinWindow::PinWindow() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
    id_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    setAttribute(Qt::WA_DeleteOnClose);
}

PinState PinWindow::layoutState() const {
    return {id_, {}, geometry(), originalSize_, windowOpacity(), bool(windowFlags() & Qt::WindowStaysOnTopHint), {}};
}

void PinWindow::restoreLayout(const PinState &state) {
    id_ = state.id;
    originalSize_ = state.originalSize;
    setWindowFlag(Qt::WindowStaysOnTopHint, state.alwaysOnTop);
    setWindowOpacity(state.opacity);
    QScreen *screen = QGuiApplication::screenAt(state.geometry.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    setScreen(screen);
    const QRect available = screen->availableGeometry();
    const QSize size = state.geometry.size().boundedTo(available.size()).expandedTo(minimumSize());
    const QRect restored(QPoint(qBound(available.left(), state.geometry.x(), available.right() - size.width() + 1),
                               qBound(available.top(), state.geometry.y(), available.bottom() - size.height() + 1)), size);
    setGeometry(restored);
    // Cocoa's initial show events can replace the position; apply it after those events as well.
    QTimer::singleShot(0, this, [this, restored] { setGeometry(restored); });
}

void PinWindow::closeEvent(QCloseEvent *event) { emit closing(state()); QWidget::closeEvent(event); }

bool PinWindow::event(QEvent *event) {
    if (event->type() == QEvent::WindowDeactivate && hideOnBlur_ && !confirmingClose_ && !QApplication::activePopupWidget()) hide();
    return QWidget::event(event);
}

} // namespace still
