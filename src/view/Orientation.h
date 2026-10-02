#pragma once
#include <QImage>
#include <QSize>
#include <QSizeF>

// Lossless capture orientation: rotation in 90-degree steps plus an optional
// horizontal mirror.
//
// Unlike zoom (spec 9), orientation IS applied to saved snapshots and
// recordings as well as to the live view. The reason the two differ: zoom
// crops, so baking it in would destroy the full-sensor-frame guarantee,
// while a 90-degree rotation of a 640x480 frame is the same 307200 pixels
// in a 480x640 arrangement and a mirror is the same pixels reordered.
// Nothing is resampled, nothing is discarded. The operator therefore gets
// evidence oriented the way they actually observed it, instead of sideways
// files someone has to re-rotate in another tool later.
//
// ORDER IS FIXED AND LOAD-BEARING: mirror first, then rotate. The live view
// and the file writers must agree exactly, and the two orders disagree for
// every rotation except 180 (mirror-then-rot90 differs from rot90-then-
// mirror by a 180-degree turn). apply() is the single source of truth for
// pixels precisely so that no caller can reimplement the order and drift.
class Orientation {
public:
    void rotateClockwise();
    void rotateCounterClockwise();
    void toggleMirror();
    void reset();

    int degrees() const { return m_degrees; }      // exactly 0, 90, 180 or 270
    bool mirrored() const { return m_mirrored; }
    bool isIdentity() const { return m_degrees == 0 && !m_mirrored; }

    // True on the quarter turns, where width and height exchange places.
    // Both transformedSize() overloads and every caller that needs to know
    // read this one predicate, so the rule cannot drift between them.
    bool swapsAxes() const { return m_degrees == 90 || m_degrees == 270; }

    // Swapped on the quarter turns. The recorder is opened with a fixed
    // frame size, so this is what startRecording() must pass -- feeding
    // 480x640 frames to an encoder opened at 640x480 corrupts the take.
    QSize transformedSize(QSize source) const;
    QSizeF transformedSize(QSizeF source) const;

    // Identity returns the source untouched (same QImage, shared data, no
    // copy) so the default path stays exactly as cheap as before this
    // feature existed.
    QImage apply(const QImage& source) const;

private:
    int m_degrees = 0;
    bool m_mirrored = false;
};
