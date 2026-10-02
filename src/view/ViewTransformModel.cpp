#include "view/ViewTransformModel.h"

qreal ViewTransformModel::contentScale() const {
    return m_t.scale();
}

qreal ViewTransformModel::contentX() const { return -m_t.pan().x() * contentScale(); }
qreal ViewTransformModel::contentY() const { return -m_t.pan().y() * contentScale(); }

bool ViewTransformModel::canPan() const {
    return m_t.canPan();
}

// Must-fix minor 11: guarded here, in core, where it is testable -- not in
// AppContext. This is called once per delivered frame (the frame size is
// how the view learns the sensor resolution), so an unconditional emit
// fires `changed()` at frame rate, re-evaluating every zoom/pan binding in
// the QML scene ~30 times a second for a value that changes once per
// pipeline.
void ViewTransformModel::setFrameSize(QSizeF size) {
    if (m_t.frameSize() == size) return;
    m_t.setFrameSize(size);
    emit changed();
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

void ViewTransformModel::zoomByCentered(qreal factor) {
    const QSizeF vp = m_t.viewportSize();
    m_t.zoomAt(factor, {vp.width() / 2.0, vp.height() / 2.0});
    emit changed();
}
