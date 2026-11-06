#include "toolicons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QGuiApplication>
#include <QPalette>

namespace still {
namespace {

class ToolIconEngine final : public QIconEngine {
public:
    ToolIconEngine(ToolIcon icon, QColor color) : icon_(icon), color_(color) {}
    QIconEngine *clone() const override { return new ToolIconEngine(icon_, color_); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, result.rect(), mode, state);
        return result;
    }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        p->save();
        p->translate(rect.topLeft());
        p->scale(rect.width() / 24.0, rect.height() / 24.0);
        p->setRenderHint(QPainter::Antialiasing);
        const QColor base = color_ == QColor("#e7edf6") && QGuiApplication::palette().color(QPalette::Window).lightness() > 128 ? QColor("#42516a") : color_;
        const QColor ink = mode == QIcon::Disabled ? QColor("#788597") : state == QIcon::On ? (base.lightness() < 160 ? QColor("#2859d7") : QColor("#8bc8ff")) : base;
        p->setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        auto pencil = [p] {
            p->drawPolygon(QPolygonF{QPointF(5, 15), QPointF(15, 5), QPointF(19, 9), QPointF(9, 19), QPointF(4, 20)});
            p->drawLine(QPointF(13, 7), QPointF(17, 11));
        };
        switch (icon_) {
        case ToolIcon::Crop:
            p->drawPolyline(QPolygonF{QPointF(6, 3), QPointF(6, 18), QPointF(21, 18)});
            p->drawPolyline(QPolygonF{QPointF(3, 6), QPointF(18, 6), QPointF(18, 21)}); break;
        case ToolIcon::FlipHorizontal:
        case ToolIcon::FlipVertical:
            if (icon_ == ToolIcon::FlipVertical) { p->translate(12, 12); p->rotate(90); p->translate(-12, -12); }
            p->drawLine(QPointF(12, 3), QPointF(12, 7)); p->drawLine(QPointF(12, 17), QPointF(12, 21));
            p->drawLine(QPointF(3, 12), QPointF(21, 12));
            p->drawPolyline(QPolygonF{QPointF(7, 8), QPointF(3, 12), QPointF(7, 16)});
            p->drawPolyline(QPolygonF{QPointF(17, 8), QPointF(21, 12), QPointF(17, 16)}); break;
        case ToolIcon::Select:
            p->drawPolygon(QPolygonF{QPointF(5, 3), QPointF(5, 21), QPointF(10, 16), QPointF(14, 22), QPointF(17, 20), QPointF(13, 14), QPointF(21, 14)}); break;
        case ToolIcon::QuickSave:
            p->drawPolyline(QPolygonF{QPointF(4, 15), QPointF(4, 20), QPointF(20, 20), QPointF(20, 15)});
            p->drawLine(QPointF(12, 3), QPointF(12, 15));
            p->drawPolyline(QPolygonF{QPointF(7, 10), QPointF(12, 15), QPointF(17, 10)}); break;
        case ToolIcon::Minimize: p->drawLine(QPointF(5, 12), QPointF(19, 12)); break;
        case ToolIcon::Maximize: p->drawRoundedRect(QRectF(5, 5, 14, 14), 1, 1); break;
        case ToolIcon::Restore:
            p->drawRoundedRect(QRectF(4, 8, 12, 12), 1, 1);
            p->drawPolyline(QPolygonF{QPointF(8, 4), QPointF(20, 4), QPointF(20, 16)}); break;
        case ToolIcon::Back:
            p->drawPolyline(QPolygonF{QPointF(14, 5), QPointF(7, 12), QPointF(14, 19)}); break;
        case ToolIcon::TextSnippet:
            p->drawLine(QPointF(5, 6), QPointF(19, 6));
            p->drawLine(QPointF(5, 12), QPointF(19, 12));
            p->drawLine(QPointF(5, 18), QPointF(13, 18)); break;
        case ToolIcon::Ellipsis:
            p->setPen(Qt::NoPen); p->setBrush(ink);
            for (int x : {5, 12, 19}) p->drawEllipse(QPointF(x, 12), 1.5, 1.5);
            break;
        case ToolIcon::Search:
            p->drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
            p->drawLine(QPointF(15.5, 15.5), QPointF(21, 21)); break;
        case ToolIcon::Preview:
            p->drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
            p->drawLine(QPointF(14, 4), QPointF(14, 20)); break;
        case ToolIcon::ChevronDown:
            p->drawPolyline(QPolygonF{QPointF(6, 9), QPointF(12, 15), QPointF(18, 9)}); break;
        case ToolIcon::Clipboard:
            p->drawRoundedRect(QRectF(5, 5, 14, 16), 2, 2);
            p->setBrush(Qt::NoBrush); p->drawRoundedRect(QRectF(8, 3, 8, 5), 1, 1);
            p->drawLine(QPointF(9, 12), QPointF(15, 12)); p->drawLine(QPointF(9, 16), QPointF(15, 16)); break;
        case ToolIcon::Files:
            p->drawRoundedRect(QRectF(8, 7, 12, 14), 2, 2);
            p->drawPolyline(QPolygonF{QPointF(15, 4), QPointF(4, 4), QPointF(4, 17)});
            p->drawLine(QPointF(11, 12), QPointF(17, 12)); p->drawLine(QPointF(11, 16), QPointF(17, 16)); break;
        case ToolIcon::Image:
            p->drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2); p->drawEllipse(QPointF(8, 9), 1.5, 1.5);
            p->drawPolyline(QPolygonF{QPointF(4, 18), QPointF(11, 12), QPointF(15, 16), QPointF(20, 11)}); break;
        case ToolIcon::Link:
            p->save(); p->translate(12, 12); p->rotate(-40);
            p->drawRoundedRect(QRectF(-9, -3.5, 11, 7), 3, 3); p->drawRoundedRect(QRectF(-2, -3.5, 11, 7), 3, 3); p->restore(); break;
        case ToolIcon::Star:
            p->drawPolygon(QPolygonF{QPointF(12, 3), QPointF(15, 9), QPointF(22, 10), QPointF(17, 15), QPointF(18, 22), QPointF(12, 19), QPointF(6, 22), QPointF(7, 15), QPointF(2, 10), QPointF(9, 9)}); break;
        case ToolIcon::Trash:
            p->drawLine(QPointF(4, 6), QPointF(20, 6)); p->drawLine(QPointF(9, 3), QPointF(15, 3));
            p->drawPolyline(QPolygonF{QPointF(6, 6), QPointF(7, 21), QPointF(17, 21), QPointF(18, 6)});
            p->drawLine(QPointF(10, 10), QPointF(10, 17)); p->drawLine(QPointF(14, 10), QPointF(14, 17)); break;
        case ToolIcon::Pause:
            p->drawRoundedRect(QRectF(5, 4, 4, 16), 1, 1); p->drawRoundedRect(QRectF(15, 4, 4, 16), 1, 1); break;
        case ToolIcon::Settings: {
            const QPolygonF tooth{{-2.8, -6.8}, {-1.7, -9.6}, {1.7, -9.6}, {2.8, -6.8}};
            QPolygonF outline;
            for (int angle = 0; angle < 360; angle += 60)
                outline += QTransform::fromTranslate(12, 12).rotate(angle).map(tooth);
            p->drawPolygon(outline);
            p->drawEllipse(QPointF(12, 12), 3, 3);
            break;
        }
        case ToolIcon::Keyboard:
            p->drawRoundedRect(QRectF(2, 5, 20, 14), 2, 2);
            for (int y : {9, 12}) for (int x : {6, 10, 14, 18}) p->drawPoint(x, y);
            p->drawLine(QPointF(7, 16), QPointF(17, 16)); break;
        case ToolIcon::Capture:
            p->drawPolyline(QPolygonF{QPointF(3, 9), QPointF(3, 3), QPointF(9, 3)});
            p->drawPolyline(QPolygonF{QPointF(15, 3), QPointF(21, 3), QPointF(21, 9)});
            p->drawPolyline(QPolygonF{QPointF(3, 15), QPointF(3, 21), QPointF(9, 21)});
            p->drawPolyline(QPolygonF{QPointF(15, 21), QPointF(21, 21), QPointF(21, 15)}); break;
        case ToolIcon::Rectangle: p->drawRect(QRectF(4, 5, 16, 14)); break;
        case ToolIcon::RoundedRectangle: p->drawRoundedRect(QRectF(4, 5, 16, 14), 4, 4); break;
        case ToolIcon::Ellipse: p->drawEllipse(QRectF(3, 5, 18, 14)); break;
        case ToolIcon::Line: p->drawLine(QPointF(5, 19), QPointF(19, 5)); break;
        case ToolIcon::Arrow:
            p->drawLine(QPointF(5, 19), QPointF(19, 5));
            p->drawPolyline(QPolygonF{QPointF(10, 5), QPointF(19, 5), QPointF(19, 14)});
            break;
        case ToolIcon::Edit:
        case ToolIcon::Pen: pencil(); break;
        case ToolIcon::Highlight:
            p->drawPolygon(QPolygonF{QPointF(8, 12), QPointF(15, 5), QPointF(20, 10), QPointF(13, 17)});
            p->drawPolyline(QPolygonF{QPointF(8, 12), QPointF(5, 17), QPointF(8, 20), QPointF(13, 17)});
            p->drawLine(QPointF(4, 21), QPointF(12, 21));
            break;
        case ToolIcon::Mosaic:
            for (int row = 0; row < 3; ++row) for (int column = 0; column < 3; ++column) {
                QColor fill = ink;
                fill.setAlpha((row + column) % 2 ? 100 : 230);
                p->fillRect(QRectF(4 + column * 6, 4 + row * 6, 4, 4), fill);
            }
            break;
        case ToolIcon::MosaicPen:
            p->drawPolygon(QPolygonF{QPointF(8, 13), QPointF(16, 5), QPointF(20, 9), QPointF(12, 17), QPointF(7, 18)});
            p->fillRect(QRectF(3, 15, 2.5, 2.5), ink);
            p->fillRect(QRectF(3, 19, 2.5, 2.5), ink);
            p->fillRect(QRectF(7, 20, 2.5, 2.5), ink);
            break;
        case ToolIcon::Text:
            p->drawLine(QPointF(5, 5), QPointF(19, 5));
            p->drawLine(QPointF(12, 5), QPointF(12, 20));
            p->drawLine(QPointF(8, 20), QPointF(16, 20));
            break;
        case ToolIcon::Number:
            p->drawLine(QPointF(10, 4), QPointF(7, 20));
            p->drawLine(QPointF(17, 4), QPointF(14, 20));
            p->drawLine(QPointF(5, 9), QPointF(20, 9));
            p->drawLine(QPointF(4, 15), QPointF(19, 15));
            break;
        case ToolIcon::Copy:
            p->drawRoundedRect(QRectF(8, 8, 12, 12), 2, 2);
            p->drawPolyline(QPolygonF{QPointF(15, 5), QPointF(15, 3), QPointF(3, 3), QPointF(3, 15), QPointF(5, 15)});
            break;
        case ToolIcon::Save:
            p->drawPolygon(QPolygonF{QPointF(4, 3), QPointF(17, 3), QPointF(21, 7), QPointF(21, 21), QPointF(4, 21)});
            p->drawRect(QRectF(8, 3, 8, 6));
            p->drawRect(QRectF(8, 14, 9, 7));
            break;
        case ToolIcon::Pin:
            p->drawPolyline(QPolygonF{QPointF(7, 4), QPointF(17, 4), QPointF(16, 10), QPointF(19, 14), QPointF(5, 14), QPointF(8, 10), QPointF(7, 4)});
            p->drawLine(QPointF(12, 14), QPointF(12, 21));
            break;
        case ToolIcon::Ocr:
            p->drawPolyline(QPolygonF{QPointF(3, 8), QPointF(3, 3), QPointF(8, 3)});
            p->drawPolyline(QPolygonF{QPointF(16, 3), QPointF(21, 3), QPointF(21, 8)});
            p->drawPolyline(QPolygonF{QPointF(3, 16), QPointF(3, 21), QPointF(8, 21)});
            p->drawPolyline(QPolygonF{QPointF(16, 21), QPointF(21, 21), QPointF(21, 16)});
            p->drawLine(QPointF(8, 8), QPointF(16, 8));
            p->drawLine(QPointF(12, 8), QPointF(12, 17));
            break;
        case ToolIcon::Scroll:
            p->drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
            p->drawLine(QPointF(12, 7), QPointF(12, 17));
            p->drawPolyline(QPolygonF{QPointF(9, 10), QPointF(12, 7), QPointF(15, 10)});
            p->drawPolyline(QPolygonF{QPointF(9, 14), QPointF(12, 17), QPointF(15, 14)});
            break;
        case ToolIcon::Close:
            p->drawLine(QPointF(6, 6), QPointF(18, 18));
            p->drawLine(QPointF(18, 6), QPointF(6, 18));
            break;
        case ToolIcon::Apply:
            p->drawPolyline(QPolygonF{QPointF(4, 12), QPointF(10, 18), QPointF(20, 6)});
            break;
        case ToolIcon::More:
            p->drawPolyline(QPolygonF{QPointF(5, 6), QPointF(11, 12), QPointF(5, 18)});
            p->drawPolyline(QPolygonF{QPointF(13, 6), QPointF(19, 12), QPointF(13, 18)});
            break;
        case ToolIcon::Color:
            p->setBrush(ink);
            p->drawEllipse(QRectF(5, 5, 14, 14));
            break;
        case ToolIcon::Redo: p->translate(24, 0); p->scale(-1, 1); [[fallthrough]];
        case ToolIcon::Undo: {
            QPainterPath arc;
            arc.moveTo(20, 18);
            arc.cubicTo(21, 8, 12, 4, 5, 10);
            p->drawPath(arc);
            p->drawPolyline(QPolygonF{QPointF(5, 5), QPointF(5, 10), QPointF(10, 10)});
            break;
        }
        }
        p->restore();
    }
private:
    ToolIcon icon_;
    QColor color_;
};

} // namespace

QIcon toolIcon(ToolIcon icon, QColor color) { return QIcon(new ToolIconEngine(icon, color)); }

QString toolbarStyleSheet() {
    if (QGuiApplication::palette().color(QPalette::Window).lightness() > 128) return QStringLiteral(R"(
        QWidget#CaptureTools { background: #f4f5f7; color: #242731; border: 1px solid #d9dde5; border-radius: 7px; }
        QToolBar#CaptureTools { spacing: 2px; padding: 5px; border-radius: 0; border: none; border-bottom: 1px solid #d9dde5; }
        #CaptureTools QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 5px; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; color: #242731; }
        #CaptureTools QToolButton:hover { background: #e2e6ed; }
        #CaptureTools QToolButton:pressed { background: #e2e6ed; }
        #CaptureTools QToolButton:checked { background: #e2e6ed; }
        #CaptureTools QToolButton:focus { border-color: #454b57; }
        #CaptureTools QToolButton:disabled { background: transparent; }
        #CaptureTools QToolButton#ImageTransforms { min-width: 32px; max-width: 32px; }
        QToolBar#CaptureTools::separator { background: #d9dde5; width: 1px; margin: 5px 6px; }
        #CaptureTools QFrame[role="separator"] { background: #d9dde5; border: none; }
        #CaptureTools QSpinBox { background: #fff; color: #242731; border: 1px solid #d9dde5; border-radius: 5px; padding: 5px; selection-background-color: #e2e6ed; selection-color: #242731; }
        #CaptureTools QSpinBox:focus { border-color: #454b57; }
        #CaptureTools QSpinBox:disabled { background: #f4f5f7; color: #858e9e; }
        #CaptureTools QComboBox#CaptureAspectRatio { background: #fff; color: #242731; border: 1px solid #d9dde5; border-radius: 5px; padding: 5px; }
        #CaptureTools QComboBox#CaptureAspectRatio:focus { border-color: #454b57; }
    )");
    return QStringLiteral(R"(
        QWidget#CaptureTools { background: #1c1c1c; color: #ededed; border: 1px solid #383838; border-radius: 7px; }
        QToolBar#CaptureTools { spacing: 2px; padding: 5px; border-radius: 0; border: none; border-bottom: 1px solid #383838; }
        #CaptureTools QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 5px; min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; color: #ededed; }
        #CaptureTools QToolButton:hover { background: #242424; }
        #CaptureTools QToolButton:pressed { background: #383838; }
        #CaptureTools QToolButton:checked { background: #292929; }
        #CaptureTools QToolButton:focus { border-color: #a8a8a8; }
        #CaptureTools QToolButton:disabled { background: transparent; }
        #CaptureTools QToolButton#ImageTransforms { min-width: 32px; max-width: 32px; }
        QToolBar#CaptureTools::separator { background: #383838; width: 1px; margin: 5px 6px; }
        #CaptureTools QFrame[role="separator"] { background: #383838; border: none; }
        #CaptureTools QSpinBox { background: #1c1c1c; color: #ededed; border: 1px solid #383838; border-radius: 5px; padding: 5px; selection-background-color: #292929; }
        #CaptureTools QSpinBox:focus { border-color: #a8a8a8; }
        #CaptureTools QSpinBox:disabled { background: #1c1c1c; color: #898989; }
        #CaptureTools QComboBox#CaptureAspectRatio { background: #1c1c1c; color: #ededed; border: 1px solid #383838; border-radius: 5px; padding: 5px; }
        #CaptureTools QComboBox#CaptureAspectRatio:focus { border-color: #a8a8a8; }
    )");
}

} // namespace still
