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

void FakeCaptureSource::fillRgb(QVideoFrame& frame, const QColor& c) const {
    QImage view(frame.bits(0), m_size.width(), m_size.height(),
                frame.bytesPerLine(0), QImage::Format_RGBX8888);
    view.fill(c);
}

// Real YUYV bytes, not RGB bytes written into a YUYV buffer. Writing RGB
// here would make the YUY2 snapshot test assert against garbage and pass
// for the wrong reason -- the exact bug it exists to catch.
void FakeCaptureSource::fillYuyv(QVideoFrame& frame, const QColor& c) const {
    const int r = c.red(), g = c.green(), b = c.blue();
    const auto clamp8 = [](int v) { return uchar(std::clamp(v, 0, 255)); };
    const uchar y = clamp8(( 66 * r + 129 * g +  25 * b + 128) / 256 + 16);
    const uchar u = clamp8((-38 * r -  74 * g + 112 * b + 128) / 256 + 128);
    const uchar v = clamp8((112 * r -  94 * g -  18 * b + 128) / 256 + 128);

    for (int row = 0; row < m_size.height(); ++row) {
        uchar* line = frame.bits(0) + row * frame.bytesPerLine(0);
        for (int x = 0; x < m_size.width(); x += 2) {
            line[x * 2 + 0] = y;   // Y0
            line[x * 2 + 1] = u;   // U
            line[x * 2 + 2] = y;   // Y1
            line[x * 2 + 3] = v;   // V
        }
    }
}

void FakeCaptureSource::emitOneFrame() {
    if (!m_running || !m_deliver) return;

    QVideoFrameFormat format(m_size, m_pixelFormat);
    QVideoFrame frame(format);
    if (!frame.map(QVideoFrame::WriteOnly)) return;

    const QColor fill = nextFillColor();
    if (m_pixelFormat == QVideoFrameFormat::Format_YUYV) fillYuyv(frame, fill);
    else                                                 fillRgb(frame, fill);
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
