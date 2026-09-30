#include "core/Frame.h"

Frame Frame::deepCopy(const QVideoFrame& src, qint64 timestampUs) {
    Frame out;
    if (!src.isValid()) return out;

    QVideoFrame frame = src;
    if (!frame.map(QVideoFrame::ReadOnly)) return out;

    // toImage() handles the colour conversion for MJPEG, YUYV, NV12 and the
    // rest. On current Qt it happens to allocate fresh storage rather than
    // aliasing the mapped buffer, but that is not documented behaviour --
    // it depends on the buffer backend and could change with a future Qt
    // version or a different QAbstractVideoBuffer implementation. copy()
    // is belt-and-braces: it is what actually guarantees this frame owns
    // its pixels regardless of what toImage() does under the hood (spec 8.3).
    out.m_image = frame.toImage().copy();
    frame.unmap();

    out.m_tsUs = timestampUs;
    return out;
}
