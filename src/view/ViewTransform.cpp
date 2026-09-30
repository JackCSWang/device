#include "view/ViewTransform.h"
#include <algorithm>

void ViewTransform::setFrameSize(QSizeF frame) { m_frame = frame; clampPan(); }
void ViewTransform::setViewportSize(QSizeF viewport) { m_viewport = viewport; clampPan(); }

qreal ViewTransform::fitScale() const {
    if (m_frame.isEmpty() || m_viewport.isEmpty()) return 1.0;
    return std::min(m_viewport.width() / m_frame.width(),
                    m_viewport.height() / m_frame.height());
}

qreal ViewTransform::scale() const { return fitScale() * m_zoom; }

QSizeF ViewTransform::visibleSize() const {
    const qreal s = scale();
    if (s <= 0) return m_frame;
    return {m_viewport.width() / s, m_viewport.height() / s};
}

QRectF ViewTransform::visibleFrameRect() const {
    const QSizeF vis = visibleSize();
    const QPointF centre = QPointF(m_frame.width() / 2.0, m_frame.height() / 2.0) + m_pan;
    return QRectF(centre.x() - vis.width() / 2.0,
                  centre.y() - vis.height() / 2.0,
                  vis.width(), vis.height());
}

// Along any axis where the visible region is at least as large as the frame
// (letterboxing), there is no pan freedom and the frame is centred. This is
// why max-pan is floored at zero rather than assumed positive.
void ViewTransform::clampPan() {
    const QSizeF vis = visibleSize();
    const qreal maxX = std::max(0.0, (m_frame.width()  - vis.width())  / 2.0);
    const qreal maxY = std::max(0.0, (m_frame.height() - vis.height()) / 2.0);
    m_pan.setX(std::clamp(m_pan.x(), -maxX, maxX));
    m_pan.setY(std::clamp(m_pan.y(), -maxY, maxY));
}

void ViewTransform::zoomAt(qreal factor, QPointF focusInViewport) {
    const qreal sBefore = scale();
    if (sBefore <= 0) return;

    const QPointF viewportCentre(m_viewport.width() / 2.0, m_viewport.height() / 2.0);
    const QPointF frameCentre(m_frame.width() / 2.0, m_frame.height() / 2.0);
    const QPointF offset = focusInViewport - viewportCentre;
    const QPointF anchor = frameCentre + m_pan + offset / sBefore;

    m_zoom = std::clamp(m_zoom * factor, MinZoom, MaxZoom);

    const qreal sAfter = scale();
    m_pan = anchor - offset / sAfter - frameCentre;
    clampPan();
}

void ViewTransform::panBy(QPointF deltaInViewport) {
    const qreal s = scale();
    if (s <= 0) return;
    m_pan -= deltaInViewport / s;
    clampPan();
}

void ViewTransform::resetToFit() {
    m_zoom = MinZoom;
    m_pan = {0, 0};
    clampPan();
}
