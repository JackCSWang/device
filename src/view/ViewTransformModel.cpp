#include "view/ViewTransformModel.h"
#include <algorithm>

qreal ViewTransformModel::contentScale() const {
    if (m_frame.isEmpty() || m_viewport.isEmpty()) return 1.0;
    const qreal fit = std::min(m_viewport.width() / m_frame.width(),
                               m_viewport.height() / m_frame.height());
    return fit * m_t.zoom();
}

qreal ViewTransformModel::contentX() const { return -m_t.pan().x() * contentScale(); }
qreal ViewTransformModel::contentY() const { return -m_t.pan().y() * contentScale(); }

bool ViewTransformModel::canPan() const {
    const QRectF vis = m_t.visibleFrameRect();
    return vis.width() < m_frame.width() - 0.5 || vis.height() < m_frame.height() - 0.5;
}

void ViewTransformModel::setFrameSize(QSizeF size) {
    m_frame = size; m_t.setFrameSize(size); emit changed();
}
void ViewTransformModel::setViewportSize(QSizeF size) {
    m_viewport = size; m_t.setViewportSize(size); emit changed();
}
void ViewTransformModel::zoomAt(qreal factor, qreal focusX, qreal focusY) {
    m_t.zoomAt(factor, {focusX, focusY}); emit changed();
}
void ViewTransformModel::panBy(qreal dx, qreal dy) {
    m_t.panBy({dx, dy}); emit changed();
}
void ViewTransformModel::resetToFit() { m_t.resetToFit(); emit changed(); }
