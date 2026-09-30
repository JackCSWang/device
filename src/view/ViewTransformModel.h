#pragma once
#include "view/ViewTransform.h"
#include <QObject>
#include <QSizeF>
#include <qqmlintegration.h>

// QML-facing wrapper. All arithmetic stays in ViewTransform; QML only binds
// to contentScale/contentX/contentY.
class ViewTransformModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal zoom READ zoom NOTIFY changed)
    Q_PROPERTY(qreal contentX READ contentX NOTIFY changed)
    Q_PROPERTY(qreal contentY READ contentY NOTIFY changed)
    Q_PROPERTY(qreal contentScale READ contentScale NOTIFY changed)
    Q_PROPERTY(bool canPan READ canPan NOTIFY changed)

public:
    using QObject::QObject;

    qreal zoom() const { return m_t.zoom(); }
    qreal contentScale() const;
    qreal contentX() const;
    qreal contentY() const;
    bool canPan() const;

    Q_INVOKABLE void setFrameSize(QSizeF size);
    Q_INVOKABLE void setViewportSize(QSizeF size);
    Q_INVOKABLE void zoomAt(qreal factor, qreal focusX, qreal focusY);
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    Q_INVOKABLE void resetToFit();

signals:
    void changed();

private:
    ViewTransform m_t;
};
