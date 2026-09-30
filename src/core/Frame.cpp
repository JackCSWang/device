#include "core/Frame.h"

Frame Frame::deepCopy(const QVideoFrame& src, qint64 timestampUs) {
    Frame out;
    if (!src.isValid()) return out;

    QVideoFrame frame = src;
    if (!frame.map(QVideoFrame::ReadOnly)) return out;

    // toImage() handles the colour conversion for MJPEG, YUYV, NV12 and the
    // rest, but may alias the mapped buffer -- which the source recycles the
    // moment we return. copy() is what makes this frame ours. Without it you
    // get snapshots that are half one frame and half the next, intermittently,
    // under load only (spec 8.3).
    out.m_image = frame.toImage().copy();
    frame.unmap();

    out.m_tsUs = timestampUs;
    return out;
}
