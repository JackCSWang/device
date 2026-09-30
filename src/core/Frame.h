#pragma once
#include <QImage>
#include <QVideoFrame>

// An owned copy of one captured frame, safe to move to another thread.
class Frame {
public:
    Frame() = default;
    static Frame deepCopy(const QVideoFrame& src, qint64 timestampUs);

    bool isValid() const { return !m_image.isNull(); }
    const QImage& image() const { return m_image; }
    qint64 timestampUs() const { return m_tsUs; }
    QSize size() const { return m_image.size(); }

private:
    QImage m_image;
    qint64 m_tsUs = -1;
};
