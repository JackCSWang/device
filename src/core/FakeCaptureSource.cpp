#include "core/FakeCaptureSource.h"

#include <algorithm>
#include <cstring>

FakeCaptureSource::FakeCaptureSource(QObject* parent) : ICaptureSource(parent) {}

bool FakeCaptureSource::start() { m_running = true; return true; }

void FakeCaptureSource::stop() {
    if (!m_running) return;
    m_running = false;
    emit stopped(StopReason::Requested, QString());
}

void FakeCaptureSource::injectDetach() {
    if (!m_running) return;
    m_running = false;
    emit stopped(StopReason::Detached, QStringLiteral("injected detach"));
}

QColor FakeCaptureSource::nextFillColor() const {
    return QColor::fromHsv((m_counter * 37) % 360, 255, 255);
}

bool FakeCaptureSource::selectNextFormat() {
    if (m_fallbacksLeft <= 0) return false;
    --m_fallbacksLeft;
    ++m_fallbacksUsed;
    return true;
}

void FakeCaptureSource::fillRgb(QVideoFrame& frame, const QColor& c, QImage::Format imageFormat) const {
    QImage view(frame.bits(0), m_size.width(), m_size.height(),
                frame.bytesPerLine(0), imageFormat);
    view.fill(c);
}

// Real YUYV bytes, not RGB bytes written into a YUYV buffer. Writing RGB
// here would make the YUY2 snapshot test assert against garbage and pass
// for the wrong reason -- the exact bug it exists to catch.
void FakeCaptureSource::fillYuyv(QVideoFrame& frame, const QColor& c) const {
    const int r = c.red(), g = c.green(), b = c.blue();
    const auto clamp8 = [](int v) { return uchar(std::clamp(v, 0, 255)); };
    // >>8 rather than /256: right shift floors, matching the canonical
    // BT.601 integer conversion; truncating division toward zero puts U/V
    // off by one for negative numerators (spec 8.3 appendix).
    const uchar y = clamp8((((66 * r + 129 * g +  25 * b + 128) >> 8)) + 16);
    const uchar u = clamp8((((-38 * r -  74 * g + 112 * b + 128) >> 8)) + 128);
    const uchar v = clamp8((((112 * r -  94 * g -  18 * b + 128) >> 8)) + 128);

    for (int row = 0; row < m_size.height(); ++row) {
        uchar* line = frame.bits(0) + row * frame.bytesPerLine(0);
        int x = 0;
        for (; x + 1 < m_size.width(); x += 2) {
            line[x * 2 + 0] = y;   // Y0
            line[x * 2 + 1] = u;   // U
            line[x * 2 + 2] = y;   // Y1
            line[x * 2 + 3] = v;   // V
        }
        if (x < m_size.width()) {
            // Odd trailing column: only one physical pixel remains here, so
            // only its Y sample and the shared U byte are written -- writing
            // the full 4-byte macropixel would run 2 bytes past the row.
            line[x * 2 + 0] = y;   // Y0
            line[x * 2 + 1] = u;   // U
        }
    }
}

void FakeCaptureSource::emitOneFrame() {
    if (!m_running || !m_deliver) return;

    QVideoFrameFormat format(m_size, m_pixelFormat);
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly)) return;

    const QColor fill = nextFillColor();
    switch (m_pixelFormat) {
    case QVideoFrameFormat::Format_YUYV:
        fillYuyv(frame, fill);
        break;
    case QVideoFrameFormat::Format_RGBX8888:
        fillRgb(frame, fill, QImage::Format_RGBX8888);
        break;
    case QVideoFrameFormat::Format_RGBA8888:
        fillRgb(frame, fill, QImage::Format_RGBA8888);
        break;
    // BGRX8888/BGRA8888 byte order (B,G,R,X / B,G,R,A) is exactly what
    // QImage's native-endian RGB32/ARGB32 formats store in memory on a
    // little-endian machine -- this project's only supported architecture
    // (x86_64) -- so these map cleanly.
    case QVideoFrameFormat::Format_BGRX8888:
        fillRgb(frame, fill, QImage::Format_RGB32);
        break;
    case QVideoFrameFormat::Format_BGRA8888:
        fillRgb(frame, fill, QImage::Format_ARGB32);
        break;
    default:
        // No QImage format shares this pixel format's byte order (e.g.
        // ARGB8888, ABGR8888), or it isn't a format this fake can
        // synthesise at all (NV12, Jpeg, ...). Silently emitting a garbage
        // or unfilled buffer let every one of these masquerade as a
        // successfully delivered frame; fail loudly instead so a consumer
        // that selects an unsupported format finds out immediately.
        frame.unmap();
        emit stopped(StopReason::Error,
                     QStringLiteral("FakeCaptureSource cannot synthesize pixel format %1")
                         .arg(QVideoFrameFormat::pixelFormatToString(m_pixelFormat)));
        m_running = false;
        return;
    }
    frame.unmap();

    m_tsUs += 33333;   // ~30 fps
    ++m_counter;
    emit frameReady(frame, m_tsUs);

    if (m_scribble && frame.map(QVideoFrame::WriteOnly)) {
        std::memset(frame.bits(0), 0,
                    size_t(frame.bytesPerLine(0)) * size_t(m_size.height()));
        frame.unmap();
    }
}
