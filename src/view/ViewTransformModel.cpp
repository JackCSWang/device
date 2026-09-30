#include "view/ViewTransformModel.h"

qreal ViewTransformModel::contentScale() const {
    return m_t.scale();
}

qreal ViewTransformModel::contentX() const { return -m_t.pan().x() * contentScale(); }
qreal ViewTransformModel::contentY() const { return -m_t.pan().y() * contentScale(); }

bool ViewTransformModel::canPan() const {
    return m_t.canPan();
}

void ViewTransformModel::setFrameSize(QSizeF size) {
    m_t.setFrameSize(size); emit changed();
}
void ViewTransformModel::setViewportSize(QSizeF size) {
    m_t.setViewportSize(size); emit changed();
}
void ViewTransformModel::zoomAt(qreal factor, qreal focusX, qreal focusY) {
    m_t.zoomAt(factor, {focusX, focusY}); emit changed();
}
void ViewTransformModel::panBy(qreal dx, qreal dy) {
    m_t.panBy({dx, dy}); emit changed();
}
void ViewTransformModel::resetToFit() { m_t.resetToFit(); emit changed(); }
