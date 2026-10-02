#include "view/Orientation.h"
#include <QTransform>

void Orientation::rotateClockwise() {
    m_degrees = (m_degrees + 90) % 360;
}

void Orientation::rotateCounterClockwise() {
    m_degrees = (m_degrees + 270) % 360;
}

void Orientation::toggleMirror() {
    m_mirrored = !m_mirrored;
}

void Orientation::reset() {
    m_degrees = 0;
    m_mirrored = false;
}

QSize Orientation::transformedSize(QSize source) const {
    return swapsAxes() ? QSize(source.height(), source.width()) : source;
}

QSizeF Orientation::transformedSize(QSizeF source) const {
    return swapsAxes() ? QSizeF(source.height(), source.width()) : source;
}

QImage Orientation::apply(const QImage& source) const {
    if (isIdentity())
        return source;

    // Mirror first, then rotate -- see the header. Written as two explicit
    // steps rather than one composed QTransform because QTransform's
    // multiplication order is easy to get backwards and a silent 180-degree
    // error here would mean every rotated snapshot disagrees with the live
    // view.
    QImage img = m_mirrored ? source.mirrored(/*horizontal=*/true, /*vertical=*/false)
                            : source;

    if (m_degrees != 0) {
        QTransform t;
        t.rotate(m_degrees);
        // FastTransformation, not Smooth: at exact quarter turns the result
        // is a pixel permutation with no resampling, so smoothing would only
        // cost time and blur evidence.
        img = img.transformed(t, Qt::FastTransformation);
    }
    return img;
}
