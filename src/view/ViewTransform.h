#pragma once
#include <QPointF>
#include <QRectF>
#include <QSizeF>

// Pure zoom/pan math for the live view. Knows nothing about cameras,
// frames, or files. Zoom 1.0 fits the whole frame in the viewport.
class ViewTransform {
public:
    static constexpr qreal MinZoom = 1.0;
    static constexpr qreal MaxZoom = 8.0;

    void setFrameSize(QSizeF frame);
    void setViewportSize(QSizeF viewport);

    qreal zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }
    QSizeF frameSize() const { return m_frame; }
    QSizeF viewportSize() const { return m_viewport; }
    qreal scale() const;
    bool canPan() const;

    void zoomAt(qreal factor, QPointF focusInViewport);
    void panBy(QPointF deltaInViewport);
    void resetToFit();

    QRectF visibleFrameRect() const;

private:
    qreal fitScale() const;      // viewport px per frame px at zoom 1
    QSizeF visibleSize() const;  // in frame px
    void clampPan();

    QSizeF m_frame{0, 0};
    QSizeF m_viewport{0, 0};
    qreal m_zoom = 1.0;
    QPointF m_pan{0, 0};
};
