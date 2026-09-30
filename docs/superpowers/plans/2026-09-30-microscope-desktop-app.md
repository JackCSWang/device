# Microscope Capture App — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A microscope capture app on Windows, macOS, and Linux — live view, digital zoom/pan, full-resolution JPEG snapshots, H.264/MP4 recording. These three platforms are the whole product; there is no mobile phase.

**Architecture:** All frame consumers sit behind one interface, `ICaptureSource`, which emits `QVideoFrame` plus a capture timestamp. `QtCaptureSource` implements it with `QCamera`; `FakeCaptureSource` implements it with a synthetic pattern and failure injection, so the whole app is testable on CI with no hardware. `CaptureController` fans frames out to a display sink and, when armed, to `SnapshotWriter` and `IRecorder`. Zoom lives entirely in `ViewTransform` and never touches frame data.

**Tech Stack:** Qt 6.8 LTS (Quick, Multimedia, Test), C++17, CMake, CTest.

**Spec:** `docs/superpowers/specs/2026-09-30-microscope-app-design.md`

## Global Constraints

Every task's requirements implicitly include this section.

- **Qt 6.8 LTS minimum.** The spec says "Qt 6". 6.8 is pinned because `QVideoFrameInput` (6.8+) is what lets a recorder accept pushed frames, which is the only way recording can be tested against `FakeCaptureSource` rather than by hand with a scope on three machines. **This is a derived constraint, not one the spec states — see "Deviations" below.**
- **C++17.** Spec §5.
- **No elevated privileges, ever.** No kernel driver, no system service, no elevated helper. Spec §4.1.
- **No network calls.** Any feature requiring one is out of scope. Spec §4.2.
- **Qt linked as shared libraries only.** Static linking breaks LGPL compliance for this project. Spec §5.
- **Zoom is view-only.** Snapshots and recordings always contain the full sensor frame at full resolution, regardless of zoom or pan. Spec §9.
- **Snapshots:** JPEG, full sensor resolution. **Video:** H.264 in MP4, **no audio track.** Spec §13.
- **Disk thresholds, exact:** refuse to start recording below **500 MB** free; stop and finalize at **100 MB**; refuse snapshot below **50 MB**. Spec §10.3.
- **Watchdog timings, exact:** no first frame within **3 s** of open → downgrade format and retry. No frame for **5 s** mid-stream → one silent reopen, then surface. Spec §10.2, §10.3.
- **Zoom range:** 1.0x to 8.0x. Spec §9.
- **Every error message states three things:** what happened, whether data was saved, and the one action to take. Spec §10.

## Review Focus

Five input classes the spec implies but never names, ordered by how likely they are to bite a technician in the field. Each has a test pinned to the task owning the code.

1. **Viewport aspect ratio differs from frame aspect ratio.** Spec §9 says "pan clamped so the viewport never leaves the frame", but a 16:9 frame in a window the user has dragged to any other shape is letterboxed — along the letterboxed axis the visible region is *larger* than the frame, so clamping must centre rather than clamp. Naive clamping lets the user pan into black void, and on desktop the window is resizable, so this is reached by ordinary use rather than an edge case. → Task 2.
2. **Two snapshots within the same second.** Spec §13 says timestamped filenames. A technician tapping the shutter twice generates the same name and the second silently overwrites the first — evidence lost with no error. → Task 4.
3. **A YUY2 frame reaching the snapshot writer.** Spec §8.5's own preference list includes YUY2, but a writer that assumes packed RGB produces garbage JPEGs. Colour conversion must be explicit. → Task 7.
4. **Start and immediately stop a recording.** A double-tap produces a zero-frame MP4. It must be either a valid file or no file — never a 0-byte `.mp4` that looks like a recording and won't open. → Task 8.
5. **Snapshot while recording.** Spec covers each alone, never both. Both must succeed and neither may drop frames from the other. → Task 9.

## Deviations from the spec

Flag these to the human partner before implementation:

1. **Qt floor raised to 6.8 LTS** (spec says "Qt 6") — see Global Constraints.
2. **Recording routes frames through us**, camera → sink → `CaptureController` → `QVideoFrameInput` → `QMediaRecorder`, rather than letting `QMediaRecorder` pull from the camera directly. Costs one hop; buys a recorder that can be driven by `FakeCaptureSource`, so the detach-mid-recording behaviour is a CI test instead of someone yanking a cable on three machines.
3. **The deep-copy rule is enforced by test, not by type.** Spec §8.3 states the rule. Making `ICaptureSource` emit an owned `Frame` would enforce it structurally but forces a ~6 MB copy per frame at 1080p30 (~180 MB/s) on the display path, which is waste on any machine and a real cost on a low-power field laptop. Instead the seam emits `QVideoFrame`, `Frame::deepCopy` is the single chokepoint, and Task 6 has `FakeCaptureSource` deliberately scribble over its buffer immediately after emitting — so any consumer that forgot to copy fails loudly in CI. Spec §7.2's structural-enforcement claim concerns zoom, which is unaffected.
4. **`ICaptureSource` is retained despite having only one real implementation.** With Android gone it is no longer a platform-abstraction boundary. It stays because `FakeCaptureSource` is what makes the failure table testable at all — the seam earns its keep on testability alone, and removing it would mean verifying every row of spec §10.3 by hand.

---

### Task 1: Project skeleton and test harness

Deliverable: `ctest` runs green on all three desktop platforms. Nothing else in this plan can be verified until this exists.

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_harness.cpp`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: CMake targets `microscope_core` (static lib, all logic) and `microscope` (the app). Tests link `microscope_core`. **All later tasks add sources to `microscope_core`, never to `microscope`** — logic in the app target is untestable.

- [ ] **Step 1: Write the root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.21)
project(microscope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Multimedia Test)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
```

- [ ] **Step 2: Write src/CMakeLists.txt**

```cmake
add_library(microscope_core STATIC)
target_include_directories(microscope_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(microscope_core PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia)

qt_add_executable(microscope main.cpp)
target_link_libraries(microscope PRIVATE microscope_core Qt6::Quick)
```

Sources are attached by later tasks with `target_sources(microscope_core PRIVATE ...)`.

- [ ] **Step 3: Write tests/CMakeLists.txt**

```cmake
function(microscope_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE microscope_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

microscope_test(test_harness)
```

- [ ] **Step 4: Write the harness smoke test**

`tests/test_harness.cpp`:

```cpp
#include <QtTest>

class TestHarness : public QObject {
    Q_OBJECT
private slots:
    void qtTestItselfWorks() { QCOMPARE(1 + 1, 2); }
};

QTEST_MAIN(TestHarness)
#include "test_harness.moc"
```

- [ ] **Step 5: Write a placeholder main.cpp**

`src/main.cpp`:

```cpp
#include <QGuiApplication>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    return 0;
}
```

- [ ] **Step 6: Write .gitignore**

```
build/
build-*/
*.user
.DS_Store
```

- [ ] **Step 7: Configure, build, and run the tests**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `test_harness` passes, 1 test total.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src tests .gitignore
git commit -m "build: CMake skeleton with Qt Test harness"
```

---

### Task 2: ViewTransform — zoom and pan math

Pure logic, no Qt Multimedia, no hardware. Owns Review Focus item 1.

**Files:**
- Create: `src/view/ViewTransform.h`
- Create: `src/view/ViewTransform.cpp`
- Test: `tests/test_view_transform.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `ViewTransform::setFrameSize(QSizeF)`, `setViewportSize(QSizeF)`
  - `qreal zoom() const`, `QPointF pan() const`
  - `void zoomAt(qreal factor, QPointF focusInViewport)`
  - `void panBy(QPointF deltaInViewport)`, `void resetToFit()`
  - `QRectF visibleFrameRect() const` — sub-rectangle of the frame currently visible, in frame pixels
  - `static constexpr qreal MinZoom = 1.0, MaxZoom = 8.0`

**Model:** zoom 1.0 means the whole frame fits the viewport (letterboxed if aspects differ). `fitScale = min(vw/fw, vh/fh)`; effective scale `s = fitScale * zoom`; visible frame region size is `viewport / s`. `pan` is the offset in frame pixels of the view centre from the frame centre.

- [ ] **Step 1: Write the failing tests**

`tests/test_view_transform.cpp`:

```cpp
#include <QtTest>
#include "view/ViewTransform.h"

class TestViewTransform : public QObject {
    Q_OBJECT

    ViewTransform wide() {           // 16:9 frame, 16:9 viewport
        ViewTransform t;
        t.setFrameSize({1920, 1080});
        t.setViewportSize({1280, 720});
        return t;
    }
    ViewTransform square() {         // 16:9 frame, square viewport (letterboxed)
        ViewTransform t;
        t.setFrameSize({1920, 1080});
        t.setViewportSize({1000, 1000});
        return t;
    }

private slots:
    void defaultsToFit() {
        auto t = wide();
        QCOMPARE(t.zoom(), 1.0);
        QCOMPARE(t.pan(), QPointF(0, 0));
        QCOMPARE(t.visibleFrameRect(), QRectF(0, 0, 1920, 1080));
    }

    void zoomClampsToMax() {
        auto t = wide();
        t.zoomAt(100.0, {640, 360});
        QCOMPARE(t.zoom(), ViewTransform::MaxZoom);
    }

    void zoomClampsToMin() {
        auto t = wide();
        t.zoomAt(0.01, {640, 360});
        QCOMPARE(t.zoom(), ViewTransform::MinZoom);
    }

    void cannotPanAtFit() {
        auto t = wide();
        t.panBy({5000, 5000});
        QCOMPARE(t.pan(), QPointF(0, 0));
    }

    void panClampsToFrameEdge() {
        auto t = wide();
        t.zoomAt(4.0, {640, 360});          // visible = 480 x 270
        t.panBy({-99999, -99999});          // drag content far left/up
        const QRectF r = t.visibleFrameRect();
        QCOMPARE(r.right(), 1920.0);
        QCOMPARE(r.bottom(), 1080.0);
    }

    // Review Focus 1: letterboxed axis must centre, not clamp to an edge.
    void letterboxedAxisStaysCentred() {
        auto t = square();
        QCOMPARE(t.zoom(), 1.0);
        t.panBy({0, 800});
        QCOMPARE(t.pan().y(), 0.0);                 // no vertical freedom
        const QRectF r = t.visibleFrameRect();
        QCOMPARE(r.left(), 0.0);
        QCOMPARE(r.width(), 1920.0);                // full width visible
        QVERIFY(r.height() > 1080.0);               // region exceeds frame
        QCOMPARE(r.center().y(), 540.0);            // centred on the frame
    }

    void letterboxedPansOnlyWhenZoomedIn() {
        auto t = square();
        t.zoomAt(4.0, {500, 500});   // s = (1000/1920)*4; visible = 480 x 480
        t.panBy({0, -99999});
        const QRectF r = t.visibleFrameRect();
        QCOMPARE(r.bottom(), 1080.0);
        QCOMPARE(r.height(), 480.0);
    }

    void zoomKeepsFocusPointFixed() {
        auto t = wide();
        const QPointF focus{300, 200};
        const QRectF before = t.visibleFrameRect();
        const qreal sBefore = 1280.0 / before.width();
        const QPointF frameAtFocusBefore =
            before.topLeft() + QPointF(focus.x() / sBefore, focus.y() / sBefore);

        t.zoomAt(2.0, focus);

        const QRectF after = t.visibleFrameRect();
        const qreal sAfter = 1280.0 / after.width();
        const QPointF frameAtFocusAfter =
            after.topLeft() + QPointF(focus.x() / sAfter, focus.y() / sAfter);

        QVERIFY(qAbs(frameAtFocusBefore.x() - frameAtFocusAfter.x()) < 0.5);
        QVERIFY(qAbs(frameAtFocusBefore.y() - frameAtFocusAfter.y()) < 0.5);
    }

    void resetToFitRestoresDefaults() {
        auto t = wide();
        t.zoomAt(6.0, {100, 100});
        t.panBy({-200, -100});
        t.resetToFit();
        QCOMPARE(t.zoom(), 1.0);
        QCOMPARE(t.pan(), QPointF(0, 0));
    }
};

QTEST_MAIN(TestViewTransform)
#include "test_view_transform.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add `microscope_test(test_view_transform)` to `tests/CMakeLists.txt`.

```bash
cmake -B build && cmake --build build 2>&1 | head -20
```

Expected: FAIL — `view/ViewTransform.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/view/ViewTransform.h`:

```cpp
#pragma once
#include <QPointF>
#include <QRectF>
#include <QSizeF>

// Pure zoom/pan math for the live view. Knows nothing about cameras,
// frames, or files. Zoom 1.0 fits the whole frame in the viewport.
class ViewTransform {
public:
    static constexpr qreal MinZoom = 1.0;
    static constexpr qreal MaxZoom = 8.0;

    void setFrameSize(QSizeF frame);
    void setViewportSize(QSizeF viewport);

    qreal zoom() const { return m_zoom; }
    QPointF pan() const { return m_pan; }

    void zoomAt(qreal factor, QPointF focusInViewport);
    void panBy(QPointF deltaInViewport);
    void resetToFit();

    QRectF visibleFrameRect() const;

private:
    qreal fitScale() const;      // viewport px per frame px at zoom 1
    qreal scale() const;         // viewport px per frame px now
    QSizeF visibleSize() const;  // in frame px
    void clampPan();

    QSizeF m_frame{0, 0};
    QSizeF m_viewport{0, 0};
    qreal m_zoom = 1.0;
    QPointF m_pan{0, 0};
};
```

- [ ] **Step 4: Write the implementation**

`src/view/ViewTransform.cpp`:

```cpp
#include "view/ViewTransform.h"
#include <algorithm>

void ViewTransform::setFrameSize(QSizeF frame) { m_frame = frame; clampPan(); }
void ViewTransform::setViewportSize(QSizeF viewport) { m_viewport = viewport; clampPan(); }

qreal ViewTransform::fitScale() const {
    if (m_frame.isEmpty() || m_viewport.isEmpty()) return 1.0;
    return std::min(m_viewport.width() / m_frame.width(),
                    m_viewport.height() / m_frame.height());
}

qreal ViewTransform::scale() const { return fitScale() * m_zoom; }

QSizeF ViewTransform::visibleSize() const {
    const qreal s = scale();
    if (s <= 0) return m_frame;
    return {m_viewport.width() / s, m_viewport.height() / s};
}

QRectF ViewTransform::visibleFrameRect() const {
    const QSizeF vis = visibleSize();
    const QPointF centre = QPointF(m_frame.width() / 2.0, m_frame.height() / 2.0) + m_pan;
    return QRectF(centre.x() - vis.width() / 2.0,
                  centre.y() - vis.height() / 2.0,
                  vis.width(), vis.height());
}

// Along any axis where the visible region is at least as large as the frame
// (letterboxing), there is no pan freedom and the frame is centred. This is
// why max-pan is floored at zero rather than assumed positive.
void ViewTransform::clampPan() {
    const QSizeF vis = visibleSize();
    const qreal maxX = std::max(0.0, (m_frame.width()  - vis.width())  / 2.0);
    const qreal maxY = std::max(0.0, (m_frame.height() - vis.height()) / 2.0);
    m_pan.setX(std::clamp(m_pan.x(), -maxX, maxX));
    m_pan.setY(std::clamp(m_pan.y(), -maxY, maxY));
}

void ViewTransform::zoomAt(qreal factor, QPointF focusInViewport) {
    const qreal sBefore = scale();
    if (sBefore <= 0) return;

    const QPointF viewportCentre(m_viewport.width() / 2.0, m_viewport.height() / 2.0);
    const QPointF frameCentre(m_frame.width() / 2.0, m_frame.height() / 2.0);
    const QPointF offset = focusInViewport - viewportCentre;
    const QPointF anchor = frameCentre + m_pan + offset / sBefore;

    m_zoom = std::clamp(m_zoom * factor, MinZoom, MaxZoom);

    const qreal sAfter = scale();
    m_pan = anchor - offset / sAfter - frameCentre;
    clampPan();
}

void ViewTransform::panBy(QPointF deltaInViewport) {
    const qreal s = scale();
    if (s <= 0) return;
    m_pan -= deltaInViewport / s;
    clampPan();
}

void ViewTransform::resetToFit() {
    m_zoom = MinZoom;
    m_pan = {0, 0};
    clampPan();
}
```

- [ ] **Step 5: Add sources and run the tests**

Add to `src/CMakeLists.txt`:

```cmake
target_sources(microscope_core PRIVATE view/ViewTransform.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_view_transform --output-on-failure
```

Expected: PASS, 9 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/view tests/test_view_transform.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(view): zoom/pan transform with letterbox-aware pan clamping"
```

---

### Task 3: DiskPolicy — the three thresholds

**Files:**
- Create: `src/storage/DiskPolicy.h`
- Create: `src/storage/DiskPolicy.cpp`
- Test: `tests/test_disk_policy.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `DiskPolicy::canStartRecording(qint64 freeBytes) -> bool`
  - `DiskPolicy::mustStopRecording(qint64 freeBytes) -> bool`
  - `DiskPolicy::canSnapshot(qint64 freeBytes) -> bool`
  - `DiskPolicy::freeBytesFor(const QString& path) -> qint64`
  - Constants `RecordStartMinBytes`, `RecordStopBytes`, `SnapshotMinBytes`

- [ ] **Step 1: Write the failing test**

`tests/test_disk_policy.cpp`:

```cpp
#include <QtTest>
#include <QDir>
#include "storage/DiskPolicy.h"

class TestDiskPolicy : public QObject {
    Q_OBJECT
    static constexpr qint64 MB = 1024LL * 1024LL;
private slots:
    void recordingNeedsFiveHundredMegabytes() {
        QVERIFY(DiskPolicy::canStartRecording(500 * MB));
        QVERIFY(DiskPolicy::canStartRecording(501 * MB));
        QVERIFY(!DiskPolicy::canStartRecording(499 * MB));
    }
    void recordingStopsAtOneHundredMegabytes() {
        QVERIFY(DiskPolicy::mustStopRecording(99 * MB));
        QVERIFY(DiskPolicy::mustStopRecording(0));
        QVERIFY(!DiskPolicy::mustStopRecording(100 * MB));
    }
    void snapshotNeedsFiftyMegabytes() {
        QVERIFY(DiskPolicy::canSnapshot(50 * MB));
        QVERIFY(!DiskPolicy::canSnapshot(49 * MB));
    }
    // A running recording must not be killed the instant it starts.
    void stopThresholdIsBelowStartThreshold() {
        QVERIFY(DiskPolicy::RecordStopBytes < DiskPolicy::RecordStartMinBytes);
    }
    void negativeFreeSpaceIsTreatedAsFull() {
        QVERIFY(!DiskPolicy::canStartRecording(-1));
        QVERIFY(!DiskPolicy::canSnapshot(-1));
        QVERIFY(DiskPolicy::mustStopRecording(-1));
    }
    void queriesRealFilesystem() {
        QVERIFY(DiskPolicy::freeBytesFor(QDir::tempPath()) > 0);
    }
};

QTEST_MAIN(TestDiskPolicy)
#include "test_disk_policy.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_disk_policy)` to `tests/CMakeLists.txt`.

```bash
cmake -B build && cmake --build build 2>&1 | head -20
```

Expected: FAIL — `storage/DiskPolicy.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/storage/DiskPolicy.h`:

```cpp
#pragma once
#include <QString>
#include <QtGlobal>

// Free-space thresholds from spec 10.3. Exact values, not guidance.
class DiskPolicy {
public:
    static constexpr qint64 RecordStartMinBytes = 500LL * 1024 * 1024;
    static constexpr qint64 RecordStopBytes     = 100LL * 1024 * 1024;
    static constexpr qint64 SnapshotMinBytes    =  50LL * 1024 * 1024;

    static bool canStartRecording(qint64 freeBytes);
    static bool mustStopRecording(qint64 freeBytes);
    static bool canSnapshot(qint64 freeBytes);

    static qint64 freeBytesFor(const QString& path);
};
```

- [ ] **Step 4: Write the implementation**

`src/storage/DiskPolicy.cpp`:

```cpp
#include "storage/DiskPolicy.h"
#include <QStorageInfo>

bool DiskPolicy::canStartRecording(qint64 freeBytes) {
    return freeBytes >= RecordStartMinBytes;
}
bool DiskPolicy::mustStopRecording(qint64 freeBytes) {
    return freeBytes < RecordStopBytes;
}
bool DiskPolicy::canSnapshot(qint64 freeBytes) {
    return freeBytes >= SnapshotMinBytes;
}
qint64 DiskPolicy::freeBytesFor(const QString& path) {
    const QStorageInfo info(path);
    return info.isValid() ? info.bytesAvailable() : -1;
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE storage/DiskPolicy.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_disk_policy --output-on-failure
```

Expected: PASS, 6 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/storage tests/test_disk_policy.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(storage): free-space thresholds for recording and snapshots"
```

---

### Task 4: CaptureNaming and OutputLocation

Owns Review Focus item 2 — the same-second collision that silently destroys evidence.

**Files:**
- Create: `src/storage/CaptureNaming.h`
- Create: `src/storage/CaptureNaming.cpp`
- Create: `src/storage/OutputLocation.h`
- Create: `src/storage/OutputLocation.cpp`
- Test: `tests/test_capture_naming.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `CaptureNaming::nextName(const QDateTime& when, const QString& extension, const ExistsFn& exists) -> QString` where `using ExistsFn = std::function<bool(const QString&)>`
  - `OutputLocation::defaultDirectory() -> QString`
  - `OutputLocation::ensureExists(const QString& dir) -> bool`

- [ ] **Step 1: Write the failing test**

`tests/test_capture_naming.cpp`:

```cpp
#include <QtTest>
#include <QSet>
#include "storage/CaptureNaming.h"
#include "storage/OutputLocation.h"

class TestCaptureNaming : public QObject {
    Q_OBJECT
    static QDateTime when() {
        return QDateTime(QDate(2026, 9, 30), QTime(14, 5, 9));
    }
private slots:
    void buildsTimestampedName() {
        auto none = [](const QString&) { return false; };
        QCOMPARE(CaptureNaming::nextName(when(), "jpg", none),
                 QStringLiteral("scope_20260930_140509.jpg"));
    }

    void respectsExtension() {
        auto none = [](const QString&) { return false; };
        QCOMPARE(CaptureNaming::nextName(when(), "mp4", none),
                 QStringLiteral("scope_20260930_140509.mp4"));
    }

    // Review Focus 2: two taps in the same second must not overwrite.
    void suffixesOnCollision() {
        QSet<QString> taken{QStringLiteral("scope_20260930_140509.jpg")};
        auto exists = [&](const QString& n) { return taken.contains(n); };
        const QString second = CaptureNaming::nextName(when(), "jpg", exists);
        QCOMPARE(second, QStringLiteral("scope_20260930_140509_2.jpg"));
        QVERIFY(!taken.contains(second));
    }

    void suffixesRepeatedlyUntilFree() {
        QSet<QString> taken{
            QStringLiteral("scope_20260930_140509.jpg"),
            QStringLiteral("scope_20260930_140509_2.jpg"),
            QStringLiteral("scope_20260930_140509_3.jpg")};
        auto exists = [&](const QString& n) { return taken.contains(n); };
        QCOMPARE(CaptureNaming::nextName(when(), "jpg", exists),
                 QStringLiteral("scope_20260930_140509_4.jpg"));
    }

    void neverReturnsATakenName() {
        QSet<QString> taken;
        auto exists = [&](const QString& n) { return taken.contains(n); };
        for (int i = 0; i < 50; ++i) {
            const QString n = CaptureNaming::nextName(when(), "jpg", exists);
            QVERIFY2(!taken.contains(n), qPrintable(n));
            taken.insert(n);
        }
        QCOMPARE(taken.size(), 50);
    }

    void defaultDirectoryIsUsableAndWritable() {
        const QString dir = OutputLocation::defaultDirectory();
        QVERIFY(!dir.isEmpty());
        QVERIFY(OutputLocation::ensureExists(dir));
        QVERIFY(QFileInfo(dir).isWritable());
    }
};

QTEST_MAIN(TestCaptureNaming)
#include "test_capture_naming.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_capture_naming)` to `tests/CMakeLists.txt`.

```bash
cmake -B build && cmake --build build 2>&1 | head -20
```

Expected: FAIL — `storage/CaptureNaming.h: No such file or directory`.

- [ ] **Step 3: Write CaptureNaming**

`src/storage/CaptureNaming.h`:

```cpp
#pragma once
#include <QDateTime>
#include <QString>
#include <functional>

// Timestamped capture filenames. Collision-free by construction: a
// technician tapping the shutter twice in one second must not overwrite
// the first image, because the overwrite would be silent.
class CaptureNaming {
public:
    using ExistsFn = std::function<bool(const QString&)>;
    static constexpr const char* Prefix = "scope";

    static QString nextName(const QDateTime& when,
                            const QString& extension,
                            const ExistsFn& exists);
};
```

`src/storage/CaptureNaming.cpp`:

```cpp
#include "storage/CaptureNaming.h"

QString CaptureNaming::nextName(const QDateTime& when,
                                const QString& extension,
                                const ExistsFn& exists) {
    const QString stamp = when.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString base  = QStringLiteral("%1_%2").arg(QLatin1String(Prefix), stamp);

    QString candidate = QStringLiteral("%1.%2").arg(base, extension);
    int n = 1;
    while (exists(candidate)) {
        ++n;
        candidate = QStringLiteral("%1_%2.%3").arg(base).arg(n).arg(extension);
    }
    return candidate;
}
```

- [ ] **Step 4: Write OutputLocation**

`src/storage/OutputLocation.h`:

```cpp
#pragma once
#include <QString>

// Where captures land: a plain, user-visible folder. No database, no index.
class OutputLocation {
public:
    static QString defaultDirectory();
    static bool ensureExists(const QString& dir);
};
```

`src/storage/OutputLocation.cpp`:

```cpp
#include "storage/OutputLocation.h"
#include <QDir>
#include <QStandardPaths>

QString OutputLocation::defaultDirectory() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("Microscope"));
}

bool OutputLocation::ensureExists(const QString& dir) {
    return QDir().mkpath(dir);
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE
    storage/CaptureNaming.cpp
    storage/OutputLocation.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_capture_naming --output-on-failure
```

Expected: PASS, 6 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/storage tests/test_capture_naming.cpp src/CMakeLists.txt
git commit -m "feat(storage): collision-free timestamped capture filenames"
```

---

### Task 5: FormatPreference — negotiation order

Spec §8.5: MJPEG 1080p → YUY2 720p → first supported.

**Files:**
- Create: `src/capture/FormatPreference.h`
- Create: `src/capture/FormatPreference.cpp`
- Test: `tests/test_format_preference.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct CaptureFormat { QVideoFrameFormat::PixelFormat pixelFormat; QSize resolution; qreal maxFrameRate; }`
  - `FormatPreference::order(const QList<CaptureFormat>&) -> QList<CaptureFormat>` — best first
  - `FormatPreference::TargetArea` — 1920*1080

- [ ] **Step 1: Write the failing test**

`tests/test_format_preference.cpp`:

```cpp
#include <QtTest>
#include "capture/FormatPreference.h"

using PF = QVideoFrameFormat::PixelFormat;

class TestFormatPreference : public QObject {
    Q_OBJECT
    static CaptureFormat f(PF p, int w, int h, qreal fps = 30.0) {
        return CaptureFormat{p, QSize(w, h), fps};
    }
private slots:
    void prefersMjpegAtTenEightyOverEverything() {
        const auto out = FormatPreference::order({
            f(PF::Format_YUYV, 1280, 720),
            f(PF::Format_Jpeg, 1920, 1080),
            f(PF::Format_YUYV, 640, 480)});
        QCOMPARE(out.first().pixelFormat, PF::Format_Jpeg);
        QCOMPARE(out.first().resolution, QSize(1920, 1080));
    }

    void prefersYuyvOverUnknownFormats() {
        const auto out = FormatPreference::order({
            f(PF::Format_NV12, 1920, 1080),
            f(PF::Format_YUYV, 1280, 720)});
        QCOMPARE(out.first().pixelFormat, PF::Format_YUYV);
    }

    void prefersLargerResolutionUpToTenEighty() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 640, 480),
            f(PF::Format_Jpeg, 1280, 720)});
        QCOMPARE(out.first().resolution, QSize(1280, 720));
    }

    // Beyond 1080p there is no benefit, and bandwidth risk rises (spec 10.2).
    void doesNotPreferAboveTenEighty() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 3840, 2160),
            f(PF::Format_Jpeg, 1920, 1080)});
        QCOMPARE(out.first().resolution, QSize(1920, 1080));
    }

    void breaksTiesByFrameRate() {
        const auto out = FormatPreference::order({
            f(PF::Format_Jpeg, 1920, 1080, 15.0),
            f(PF::Format_Jpeg, 1920, 1080, 30.0)});
        QCOMPARE(out.first().maxFrameRate, 30.0);
    }

    void keepsEveryFormatSoFallbackAlwaysHasSomethingToTry() {
        const QList<CaptureFormat> in{
            f(PF::Format_NV12, 320, 240),
            f(PF::Format_Jpeg, 1920, 1080),
            f(PF::Format_YUYV, 1280, 720)};
        QCOMPARE(FormatPreference::order(in).size(), in.size());
    }

    void handlesEmptyInput() {
        QVERIFY(FormatPreference::order({}).isEmpty());
    }
};

QTEST_MAIN(TestFormatPreference)
#include "test_format_preference.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_format_preference)` to `tests/CMakeLists.txt`.

Expected: FAIL — `capture/FormatPreference.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/capture/FormatPreference.h`:

```cpp
#pragma once
#include <QList>
#include <QSize>
#include <QVideoFrameFormat>

struct CaptureFormat {
    QVideoFrameFormat::PixelFormat pixelFormat = QVideoFrameFormat::Format_Invalid;
    QSize resolution;
    qreal maxFrameRate = 0.0;
};

// Orders advertised formats best-first. The full list is always returned,
// never filtered, because the watchdog in CaptureController walks it as a
// fallback chain when a format opens but never delivers frames.
class FormatPreference {
public:
    static constexpr int TargetArea = 1920 * 1080;
    static QList<CaptureFormat> order(const QList<CaptureFormat>& advertised);
};
```

- [ ] **Step 4: Write the implementation**

`src/capture/FormatPreference.cpp`:

```cpp
#include "capture/FormatPreference.h"
#include <algorithm>

namespace {
int formatRank(QVideoFrameFormat::PixelFormat p) {
    switch (p) {
    case QVideoFrameFormat::Format_Jpeg: return 2;   // MJPEG: least bus bandwidth
    case QVideoFrameFormat::Format_YUYV: return 1;   // uncompressed fallback
    default:                             return 0;
    }
}

// Area, but capped: above 1080p there is no inspection benefit and
// isochronous bandwidth risk rises (spec 10.2).
int cappedArea(const QSize& s) {
    return std::min(s.width() * s.height(), FormatPreference::TargetArea);
}
} // namespace

QList<CaptureFormat> FormatPreference::order(const QList<CaptureFormat>& advertised) {
    QList<CaptureFormat> out = advertised;
    std::stable_sort(out.begin(), out.end(),
                     [](const CaptureFormat& a, const CaptureFormat& b) {
        const int ra = formatRank(a.pixelFormat), rb = formatRank(b.pixelFormat);
        if (ra != rb) return ra > rb;
        const int aa = cappedArea(a.resolution), ab = cappedArea(b.resolution);
        if (aa != ab) return aa > ab;
        return a.maxFrameRate > b.maxFrameRate;
    });
    return out;
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE capture/FormatPreference.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_format_preference --output-on-failure
```

Expected: PASS, 7 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/capture tests/test_format_preference.cpp src/CMakeLists.txt
git commit -m "feat(capture): format negotiation preference ordering"
```

---

### Task 6: The seam — ICaptureSource, Frame, FakeCaptureSource

The most important task in the plan. Everything downstream is testable only because of this.

**Files:**
- Create: `src/core/Frame.h`, `src/core/Frame.cpp`
- Create: `src/core/ICaptureSource.h`
- Create: `src/core/FakeCaptureSource.h`, `src/core/FakeCaptureSource.cpp`
- Test: `tests/test_fake_capture_source.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class StopReason { Requested, Detached, Error }`
  - `class ICaptureSource : public QObject` with pure virtuals `bool start()`, `void stop()`, `QSize frameSize() const`; signals `frameReady(const QVideoFrame&, qint64 timestampUs)` and `stopped(StopReason, QString detail)`
  - `Frame::deepCopy(const QVideoFrame&, qint64 timestampUs) -> Frame`; `Frame::image() -> const QImage&`; `Frame::timestampUs() -> qint64`; `Frame::isValid() -> bool`
  - `FakeCaptureSource` with `setFrameSize(QSize)`, `setPixelFormat(QVideoFrameFormat::PixelFormat)`, `emitOneFrame()`, `injectDetach()`, `setScribbleAfterEmit(bool)`, `setDeliverFrames(bool)`

- [ ] **Step 1: Write the failing test**

`tests/test_fake_capture_source.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include "core/FakeCaptureSource.h"
#include "core/Frame.h"

class TestFakeCaptureSource : public QObject {
    Q_OBJECT
private slots:
    void emitsFramesWithMonotonicTimestamps() {
        FakeCaptureSource src;
        src.setFrameSize({640, 480});
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::frameReady);
        src.emitOneFrame();
        src.emitOneFrame();
        QCOMPARE(spy.count(), 2);
        const qint64 t0 = spy.at(0).at(1).toLongLong();
        const qint64 t1 = spy.at(1).at(1).toLongLong();
        QVERIFY(t1 > t0);
    }

    void reportsFrameSize() {
        FakeCaptureSource src;
        src.setFrameSize({1920, 1080});
        QCOMPARE(src.frameSize(), QSize(1920, 1080));
    }

    void stopEmitsRequested() {
        FakeCaptureSource src;
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::stopped);
        src.stop();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<StopReason>(), StopReason::Requested);
    }

    void injectDetachEmitsDetached() {
        FakeCaptureSource src;
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::stopped);
        src.injectDetach();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<StopReason>(), StopReason::Detached);
    }

    void deliverFramesDisabledEmitsNothing() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        QVERIFY(src.start());
        QSignalSpy spy(&src, &ICaptureSource::frameReady);
        src.emitOneFrame();
        QCOMPARE(spy.count(), 0);
    }

    void frameDeepCopyOwnsItsPixels() {
        FakeCaptureSource src;
        src.setFrameSize({64, 48});
        src.setScribbleAfterEmit(true);   // buffer is overwritten after emit
        QVERIFY(src.start());

        Frame captured;
        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) {
                    captured = Frame::deepCopy(f, ts);
                });
        const QColor expected = src.nextFillColor();
        src.emitOneFrame();

        QVERIFY(captured.isValid());
        QCOMPARE(captured.image().size(), QSize(64, 48));
        // If deepCopy shared the source buffer, scribbling would show here.
        QCOMPARE(captured.image().pixelColor(32, 24).rgb(), expected.rgb());
    }

    void deepCopyOfInvalidFrameIsInvalid() {
        QVERIFY(!Frame::deepCopy(QVideoFrame(), 0).isValid());
    }
};

QTEST_MAIN(TestFakeCaptureSource)
#include "test_fake_capture_source.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_fake_capture_source)` to `tests/CMakeLists.txt`.

Expected: FAIL — `core/FakeCaptureSource.h: No such file or directory`.

- [ ] **Step 3: Write ICaptureSource**

`src/core/ICaptureSource.h`:

```cpp
#pragma once
#include <QObject>
#include <QSize>
#include <QVideoFrame>

enum class StopReason { Requested, Detached, Error };

// Required so QSignalSpy can round-trip the enum through QVariant. Without
// it, spy.at(0).at(0).value<StopReason>() silently returns Requested for
// every reason and the detach tests pass while proving nothing.
Q_DECLARE_METATYPE(StopReason)

// The one seam in the system. QtCaptureSource implements it with QCamera;
// FakeCaptureSource implements it with a synthetic pattern and failure
// injection. Nothing downstream knows which it has, which is what lets the
// whole failure table be tested with no scope attached.
//
// frameReady delivers a borrowed QVideoFrame: the underlying buffer belongs
// to the source and is recycled. Any consumer keeping it past the slot MUST
// call Frame::deepCopy first (spec 8.3).
class ICaptureSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ICaptureSource() override = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual QSize frameSize() const = 0;

    // Advances to the next format in the preference chain, for the watchdog's
    // downgrade-and-retry (spec 10.2). Sources with nothing to fall back to
    // return false, which is why this is virtual rather than pure.
    virtual bool selectNextFormat() { return false; }

signals:
    void frameReady(const QVideoFrame& frame, qint64 timestampUs);
    void stopped(StopReason reason, QString detail);
};
```

- [ ] **Step 4: Write Frame**

`src/core/Frame.h`:

```cpp
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
```

`src/core/Frame.cpp`:

```cpp
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
```

- [ ] **Step 5: Write FakeCaptureSource**

`src/core/FakeCaptureSource.h`:

```cpp
#pragma once
#include "core/ICaptureSource.h"
#include <QColor>
#include <QVideoFrameFormat>

// Synthetic source for tests and CI. Emits a flat frame in a known colour
// that advances each frame, so a consumer can assert exact pixels.
//
// Failure injection exists so every row of spec 10.3 gets an automated test
// with no scope attached.
class FakeCaptureSource : public ICaptureSource {
    Q_OBJECT
public:
    explicit FakeCaptureSource(QObject* parent = nullptr);

    bool start() override;
    void stop() override;
    QSize frameSize() const override { return m_size; }
    bool selectNextFormat() override;

    void setFrameSize(QSize size) { m_size = size; }
    void setPixelFormat(QVideoFrameFormat::PixelFormat f) { m_pixelFormat = f; }
    void setDeliverFrames(bool on) { m_deliver = on; }

    // How many times selectNextFormat() may succeed before the chain is
    // exhausted. Lets the watchdog's fallback path be tested end to end.
    void setFallbackCount(int n) { m_fallbacksLeft = n; }
    int fallbacksUsed() const { return m_fallbacksUsed; }

    // After emitting, overwrite the frame buffer. Any consumer that failed to
    // deep-copy will see corruption, and its test will fail.
    void setScribbleAfterEmit(bool on) { m_scribble = on; }

    void emitOneFrame();
    void injectDetach();

    QColor nextFillColor() const;

private:
    void fillRgb(QVideoFrame& frame, const QColor& c) const;
    void fillYuyv(QVideoFrame& frame, const QColor& c) const;

    QSize m_size{640, 480};
    QVideoFrameFormat::PixelFormat m_pixelFormat = QVideoFrameFormat::Format_RGBX8888;
    bool m_running = false;
    bool m_deliver = true;
    bool m_scribble = false;
    int m_counter = 0;
    int m_fallbacksLeft = 0;
    int m_fallbacksUsed = 0;
    qint64 m_tsUs = 0;
};
```

`src/core/FakeCaptureSource.cpp`:

```cpp
#include "core/FakeCaptureSource.h"

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
```

Add `#include <algorithm>` and `#include <cstring>` at the top of the file.

- [ ] **Step 6: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE core/Frame.cpp core/FakeCaptureSource.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_fake_capture_source --output-on-failure
```

Expected: PASS, 7 test functions. `frameDeepCopyOwnsItsPixels` is the one that matters — it fails if `Frame.cpp` drops the `.copy()`.

- [ ] **Step 7: Commit**

```bash
git add src/core tests/test_fake_capture_source.cpp src/CMakeLists.txt
git commit -m "feat(core): ICaptureSource seam, owned Frame, fake source with failure injection"
```

---

### Task 7: SnapshotWriter

Owns Review Focus item 3 — YUY2 frames must not become garbage JPEGs.

**Files:**
- Create: `src/output/SnapshotWriter.h`, `src/output/SnapshotWriter.cpp`
- Test: `tests/test_snapshot_writer.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Frame::deepCopy` (Task 6).
- Produces:
  - `SnapshotWriter::write(const QVideoFrame& frame, qint64 timestampUs, const QString& path)` — deep-copies on the calling thread, encodes on `QThreadPool`
  - signals `written(QString path, QSize size)`, `failed(QString path, QString reason)`

- [ ] **Step 1: Write the failing test**

`tests/test_snapshot_writer.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImageReader>
#include "core/FakeCaptureSource.h"
#include "output/SnapshotWriter.h"

class TestSnapshotWriter : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString path(const QString& name) { return m_dir.filePath(name); }

private slots:
    void writesFullResolutionJpeg() {
        FakeCaptureSource src;
        src.setFrameSize({1920, 1080});
        src.start();

        SnapshotWriter writer;
        QSignalSpy spy(&writer, &SnapshotWriter::written);
        const QString out = path(QStringLiteral("a.jpg"));

        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) { writer.write(f, ts, out); });
        src.emitOneFrame();

        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(1).toSize(), QSize(1920, 1080));

        QImageReader reader(out);
        QCOMPARE(reader.format(), QByteArray("jpeg"));
        QCOMPARE(reader.size(), QSize(1920, 1080));
    }

    // Review Focus 3: the spec's own preference list includes YUY2.
    void writesCorrectPixelsFromYuyvFrames() {
        FakeCaptureSource src;
        src.setFrameSize({320, 240});
        src.setPixelFormat(QVideoFrameFormat::Format_YUYV);
        src.start();

        SnapshotWriter writer;
        QSignalSpy spy(&writer, &SnapshotWriter::written);
        const QString out = path(QStringLiteral("yuyv.jpg"));
        const QColor expected = src.nextFillColor();

        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) { writer.write(f, ts, out); });
        src.emitOneFrame();

        QVERIFY(spy.wait(5000));
        const QImage back(out);
        QVERIFY(!back.isNull());
        QCOMPARE(back.size(), QSize(320, 240));

        // The actual colour must survive YUV -> RGB -> JPEG. A format mix-up
        // yields a grey smear or a wildly wrong hue; asserting "not grey"
        // alone would pass on garbage, so assert the hue.
        const QColor got = back.pixelColor(160, 120);
        QVERIFY2(got.saturation() > 40, "colour was lost entirely");
        const int hueError = qAbs(got.hue() - expected.hue());
        QVERIFY2(qMin(hueError, 360 - hueError) < 20,
                 qPrintable(QStringLiteral("hue %1, expected %2")
                            .arg(got.hue()).arg(expected.hue())));
    }

    void survivesSourceRecyclingItsBuffer() {
        FakeCaptureSource src;
        src.setFrameSize({64, 48});
        src.setScribbleAfterEmit(true);
        src.start();

        SnapshotWriter writer;
        QSignalSpy spy(&writer, &SnapshotWriter::written);
        const QString out = path(QStringLiteral("race.jpg"));
        const QColor expected = src.nextFillColor();

        connect(&src, &ICaptureSource::frameReady, this,
                [&](const QVideoFrame& f, qint64 ts) { writer.write(f, ts, out); });
        src.emitOneFrame();

        QVERIFY(spy.wait(5000));
        const QImage back(out);
        QCOMPARE(back.pixelColor(32, 24).hue(), expected.hue());
    }

    void failsLoudlyOnUnwritablePath() {
        FakeCaptureSource src;
        src.start();
        SnapshotWriter writer;
        QSignalSpy failures(&writer, &SnapshotWriter::failed);

        connect(&src, &ICaptureSource::frameReady, this, [&](const QVideoFrame& f, qint64 ts) {
            writer.write(f, ts, m_dir.filePath(QStringLiteral("nope/deeper/x.jpg")));
        });
        src.emitOneFrame();

        QVERIFY(failures.wait(5000));
        QVERIFY(!failures.at(0).at(1).toString().isEmpty());
    }

    void invalidFrameFailsRatherThanWritingAnEmptyFile() {
        SnapshotWriter writer;
        QSignalSpy failures(&writer, &SnapshotWriter::failed);
        const QString out = path(QStringLiteral("invalid.jpg"));
        writer.write(QVideoFrame(), 0, out);
        QVERIFY(failures.wait(5000));
        QVERIFY(!QFile::exists(out));
    }
};

QTEST_MAIN(TestSnapshotWriter)
#include "test_snapshot_writer.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_snapshot_writer)` to `tests/CMakeLists.txt`.

Expected: FAIL — `output/SnapshotWriter.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/output/SnapshotWriter.h`:

```cpp
#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <QVideoFrame>

// Writes full-resolution JPEG snapshots. Zoom state is deliberately not a
// parameter and there is no way to pass one: snapshots are physically
// incapable of being cropped (spec 7.2, 9).
//
// The deep copy happens synchronously on the calling thread; JPEG encoding
// happens on the global thread pool, because encoding a 1080p frame takes
// tens of milliseconds -- a visible stutter on the UI thread, and dropped
// frames on the capture thread (spec 8.2).
class SnapshotWriter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void write(const QVideoFrame& frame, qint64 timestampUs, const QString& path);

signals:
    void written(QString path, QSize size);
    void failed(QString path, QString reason);
};
```

- [ ] **Step 4: Write the implementation**

`src/output/SnapshotWriter.cpp`:

```cpp
#include "output/SnapshotWriter.h"
#include "core/Frame.h"
#include <QThreadPool>
#include <QtConcurrent>

namespace { constexpr int JpegQuality = 92; }

void SnapshotWriter::write(const QVideoFrame& frame, qint64 timestampUs,
                           const QString& path) {
    // Copy now, on this thread, while the buffer is still valid.
    const Frame owned = Frame::deepCopy(frame, timestampUs);
    if (!owned.isValid()) {
        emit failed(path, tr("The frame could not be read. Nothing was saved."));
        return;
    }

    const QImage image = owned.image();
    QThreadPool::globalInstance()->start([this, image, path] {
        if (image.save(path, "JPEG", JpegQuality))
            emit written(path, image.size());
        else
            emit failed(path, tr("Could not write to %1. Nothing was saved. "
                                 "Check the folder exists and has free space.").arg(path));
    });
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE output/SnapshotWriter.cpp)
target_link_libraries(microscope_core PUBLIC Qt6::Concurrent)
```

Add `Concurrent` to the root `find_package` components.

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_snapshot_writer --output-on-failure
```

Expected: PASS, 5 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/output tests/test_snapshot_writer.cpp src/CMakeLists.txt CMakeLists.txt
git commit -m "feat(output): full-resolution JPEG snapshot writer on thread pool"
```

---

### Task 8: IRecorder and QtRecorder

Owns Review Focus item 4 — a double-tap must not leave a 0-byte `.mp4`.

**Files:**
- Create: `src/output/IRecorder.h`
- Create: `src/output/QtRecorder.h`, `src/output/QtRecorder.cpp`
- Test: `tests/test_qt_recorder.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FakeCaptureSource` (Task 6).
- Produces:
  - `class IRecorder : public QObject` with `bool start(const QString& path, const QSize& size, qreal frameRate)`, `void feed(const QVideoFrame&, qint64 ptsUs)`, `void finalizeAndStop()`, `bool isRecording() const`; signals `finished(QString path, qint64 durationUs)`, `failed(QString path, QString reason)`
  - `QtRecorder` implementing it via `QVideoFrameInput` → `QMediaCaptureSession` → `QMediaRecorder`

**Why `QVideoFrameInput`:** it lets the recorder accept pushed frames, so recording — including the detach-mid-recording finalize in Task 9 — is testable against the fake source. Requires Qt 6.8; see Global Constraints.

- [ ] **Step 1: Write the failing test**

`tests/test_qt_recorder.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QMediaPlayer>
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"

class TestQtRecorder : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    static qint64 durationOf(const QString& path) {
        QMediaPlayer player;
        QSignalSpy loaded(&player, &QMediaPlayer::mediaStatusChanged);
        player.setSource(QUrl::fromLocalFile(path));
        for (int i = 0; i < 50 && player.duration() == 0; ++i)
            QTest::qWait(100);
        return player.duration();   // ms
    }

private slots:
    void recordsAPlayableMp4() {
        FakeCaptureSource src;
        src.setFrameSize({640, 480});
        src.start();

        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("clip.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);

        QVERIFY(rec.start(out, {640, 480}, 30.0));
        QVERIFY(rec.isRecording());

        connect(&src, &ICaptureSource::frameReady, &rec,
                [&](const QVideoFrame& f, qint64 ts) { rec.feed(f, ts); });
        for (int i = 0; i < 60; ++i) { src.emitOneFrame(); QTest::qWait(16); }

        rec.finalizeAndStop();
        QVERIFY(done.wait(15000));
        QVERIFY(!rec.isRecording());

        QVERIFY(QFileInfo(out).size() > 1024);
        QVERIFY(durationOf(out) > 500);       // roughly 2s of frames
    }

    // Review Focus 4: an accidental double-tap.
    void startThenImmediateStopLeavesNoBrokenFile() {
        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("empty.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);
        QSignalSpy bad(&rec, &IRecorder::failed);

        QVERIFY(rec.start(out, {640, 480}, 30.0));
        rec.finalizeAndStop();
        QVERIFY(done.wait(15000) || bad.count() > 0);

        // Either a valid file or no file. Never a 0-byte .mp4 that looks
        // like a recording and will not open.
        if (QFile::exists(out))
            QVERIFY2(QFileInfo(out).size() > 1024, "zero-frame file left on disk");
    }

    void feedBeforeStartIsIgnoredNotCrashing() {
        FakeCaptureSource src;
        src.start();
        QtRecorder rec;
        connect(&src, &ICaptureSource::frameReady, &rec,
                [&](const QVideoFrame& f, qint64 ts) { rec.feed(f, ts); });
        src.emitOneFrame();
        QVERIFY(!rec.isRecording());
    }

    void doubleStartIsRejected() {
        QtRecorder rec;
        QVERIFY(rec.start(m_dir.filePath(QStringLiteral("one.mp4")), {640, 480}, 30.0));
        QVERIFY(!rec.start(m_dir.filePath(QStringLiteral("two.mp4")), {640, 480}, 30.0));
        rec.finalizeAndStop();
    }

    void usesFrameTimestampsAsPts() {
        FakeCaptureSource src;
        src.setFrameSize({320, 240});
        src.start();

        QtRecorder rec;
        const QString out = m_dir.filePath(QStringLiteral("sparse.mp4"));
        QSignalSpy done(&rec, &IRecorder::finished);
        QVERIFY(rec.start(out, {320, 240}, 30.0));

        // 30 frames stamped 100ms apart: 3s of wall clock, not 1s.
        for (int i = 0; i < 30; ++i) {
            QVideoFrameFormat fmt({320, 240}, QVideoFrameFormat::Format_RGBX8888);
            QVideoFrame f(fmt);
            if (f.map(QVideoFrame::WriteOnly)) {
                QImage v(f.bits(0), 320, 240, f.bytesPerLine(0), QImage::Format_RGBX8888);
                v.fill(QColor::fromHsv((i * 11) % 360, 255, 255));
                f.unmap();
            }
            rec.feed(f, i * 100000LL);
            QTest::qWait(10);
        }
        rec.finalizeAndStop();
        QVERIFY(done.wait(15000));

        const qint64 ms = durationOf(out);
        QVERIFY2(ms > 2000, qPrintable(QStringLiteral("duration was %1ms").arg(ms)));
    }
};

QTEST_MAIN(TestQtRecorder)
#include "test_qt_recorder.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_qt_recorder)` to `tests/CMakeLists.txt`.

Expected: FAIL — `output/QtRecorder.h: No such file or directory`.

- [ ] **Step 3: Write IRecorder**

`src/output/IRecorder.h`:

```cpp
#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <QVideoFrame>

// Frames are pushed in with their capture timestamp as PTS, so video is
// written variable-frame-rate. If the encoder falls behind on weak hardware
// the result is a lower frame rate that still plays at correct wall-clock
// speed -- not a file that plays fast, which for inspection evidence would
// be actively misleading (spec 8.4).
class IRecorder : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IRecorder() override = default;

    virtual bool start(const QString& path, const QSize& size, qreal frameRate) = 0;
    virtual void feed(const QVideoFrame& frame, qint64 ptsUs) = 0;

    // Drains the encoder and closes the muxer. An MP4 without its moov atom
    // is unplayable, so this must run even on unexpected detach (spec 10.1).
    virtual void finalizeAndStop() = 0;

    virtual bool isRecording() const = 0;

signals:
    void finished(QString path, qint64 durationUs);
    void failed(QString path, QString reason);
};
```

- [ ] **Step 4: Write QtRecorder**

`src/output/QtRecorder.h`:

```cpp
#pragma once
#include "output/IRecorder.h"
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoFrameInput>
#include <memory>

class QtRecorder : public IRecorder {
    Q_OBJECT
public:
    explicit QtRecorder(QObject* parent = nullptr);

    bool start(const QString& path, const QSize& size, qreal frameRate) override;
    void feed(const QVideoFrame& frame, qint64 ptsUs) override;
    void finalizeAndStop() override;
    bool isRecording() const override { return m_recording; }

private:
    void onRecorderStateChanged(QMediaRecorder::RecorderState state);

    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QMediaRecorder> m_recorder;
    std::unique_ptr<QVideoFrameInput> m_input;
    QString m_path;
    bool m_recording = false;
    qint64 m_firstPtsUs = -1;
    qint64 m_lastPtsUs = -1;
    int m_framesFed = 0;
};
```

`src/output/QtRecorder.cpp`:

```cpp
#include "output/QtRecorder.h"
#include <QFile>
#include <QMediaFormat>
#include <QUrl>

QtRecorder::QtRecorder(QObject* parent) : IRecorder(parent) {}

bool QtRecorder::start(const QString& path, const QSize& size, qreal frameRate) {
    if (m_recording) return false;

    m_session  = std::make_unique<QMediaCaptureSession>();
    m_recorder = std::make_unique<QMediaRecorder>();
    m_input    = std::make_unique<QVideoFrameInput>();

    QMediaFormat format;
    format.setFileFormat(QMediaFormat::MPEG4);
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    m_recorder->setMediaFormat(format);
    m_recorder->setVideoResolution(size);
    m_recorder->setVideoFrameRate(frameRate);
    m_recorder->setQuality(QMediaRecorder::HighQuality);
    m_recorder->setOutputLocation(QUrl::fromLocalFile(path));

    m_session->setVideoFrameInput(m_input.get());
    m_session->setRecorder(m_recorder.get());

    connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged,
            this, &QtRecorder::onRecorderStateChanged);
    connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString& s) {
                emit failed(m_path, tr("Recording failed: %1. "
                                       "The file may be incomplete.").arg(s));
            });

    m_path = path;
    m_firstPtsUs = m_lastPtsUs = -1;
    m_framesFed = 0;
    m_recording = true;
    m_recorder->record();
    return true;
}

void QtRecorder::feed(const QVideoFrame& frame, qint64 ptsUs) {
    if (!m_recording || !m_input || !frame.isValid()) return;

    if (m_firstPtsUs < 0) m_firstPtsUs = ptsUs;
    m_lastPtsUs = ptsUs;

    QVideoFrame stamped = frame;
    stamped.setStartTime(ptsUs - m_firstPtsUs);
    stamped.setEndTime(ptsUs - m_firstPtsUs);

    if (m_input->sendVideoFrame(stamped)) ++m_framesFed;
    // A rejected frame means the encoder is saturated. Dropping it is correct:
    // timestamps carry the timing, so playback speed stays honest.
}

void QtRecorder::finalizeAndStop() {
    if (!m_recording) return;
    m_recording = false;
    if (m_recorder) m_recorder->stop();
}

void QtRecorder::onRecorderStateChanged(QMediaRecorder::RecorderState state) {
    if (state != QMediaRecorder::StoppedState) return;

    const qint64 durationUs =
        (m_framesFed > 0 && m_lastPtsUs >= m_firstPtsUs) ? m_lastPtsUs - m_firstPtsUs : 0;

    // A zero-frame recording produces a file that looks like a take and will
    // not open. Remove it rather than hand someone broken evidence.
    if (m_framesFed == 0) {
        QFile::remove(m_path);
        emit failed(m_path, tr("No frames were recorded, so no file was saved."));
        return;
    }
    emit finished(m_path, durationUs);
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE output/QtRecorder.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_qt_recorder --output-on-failure
```

Expected: PASS, 5 test functions. `usesFrameTimestampsAsPts` is the one that proves playback speed stays honest.

- [ ] **Step 6: Commit**

```bash
git add src/output tests/test_qt_recorder.cpp src/CMakeLists.txt
git commit -m "feat(output): IRecorder plus QVideoFrameInput-backed desktop recorder"
```

---

### Task 9: CaptureController — orchestration, watchdogs, detach handling

The behavioural heart of the app. Owns Review Focus item 5 and spec §10.1's defining field failure.

**Files:**
- Create: `src/capture/CaptureController.h`, `src/capture/CaptureController.cpp`
- Test: `tests/test_capture_controller.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ICaptureSource`, `Frame` (7), `SnapshotWriter` (8), `IRecorder` (9), `DiskPolicy` (4), `CaptureNaming`/`OutputLocation` (5).
- Produces:
  - `CaptureController(ICaptureSource*, IRecorder*, SnapshotWriter*, QString outputDir, QObject* parent = nullptr)`
  - `void begin()`, `void takeSnapshot()`, `void startRecording()`, `void stopRecording()`
  - `bool isRecording() const`, `QVideoSink* displaySink() const`
  - signals `status(QString message)`, `snapshotSaved(QString path)`, `recordingSaved(QString path)`, `sourceLost(QString message)`, `firstFrameTimedOut()`
  - `static constexpr int FirstFrameTimeoutMs = 3000, StallTimeoutMs = 5000`

- [ ] **Step 1: Write the failing test**

`tests/test_capture_controller.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

class TestCaptureController : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void snapshotWritesAFileAndReportsIt() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        c.begin();
        src.emitOneFrame();
        c.takeSnapshot();
        src.emitOneFrame();

        QVERIFY(saved.wait(5000));
        const QString path = saved.at(0).at(0).toString();
        QVERIFY(QFileInfo(path).size() > 0);
        QVERIFY(path.endsWith(QStringLiteral(".jpg")));
    }

    // Spec 10.1: the defining field failure.
    void detachMidRecordingFinalizesAndNamesTheFile() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);
        QSignalSpy lost(&c, &CaptureController::sourceLost);

        c.begin();
        c.startRecording();
        for (int i = 0; i < 45; ++i) { src.emitOneFrame(); QTest::qWait(16); }

        src.injectDetach();

        QVERIFY(savedRec.wait(15000));
        const QString path = savedRec.at(0).at(0).toString();
        QVERIFY2(QFileInfo(path).size() > 1024, "recording was not finalized");
        QCOMPARE(lost.count(), 1);
        // The message must name the saved file, or the technician assumes
        // the take was lost.
        QVERIFY(lost.at(0).at(0).toString().contains(QFileInfo(path).fileName()));
        QVERIFY(!c.isRecording());
    }

    // Review Focus 5: both at once, neither starving the other.
    void snapshotWhileRecordingSucceedsForBoth() {
        FakeCaptureSource src; src.setFrameSize({640, 480});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy snap(&c, &CaptureController::snapshotSaved);
        QSignalSpy savedRec(&c, &CaptureController::recordingSaved);

        c.begin();
        c.startRecording();
        for (int i = 0; i < 20; ++i) { src.emitOneFrame(); QTest::qWait(16); }
        c.takeSnapshot();
        for (int i = 0; i < 20; ++i) { src.emitOneFrame(); QTest::qWait(16); }
        c.stopRecording();

        QVERIFY(snap.wait(5000));
        QVERIFY(savedRec.wait(15000));
        QVERIFY(QFileInfo(snap.at(0).at(0).toString()).size() > 0);
        QVERIFY(QFileInfo(savedRec.at(0).at(0).toString()).size() > 1024);
    }

    // Spec 10.2: open() succeeding is not evidence that streaming works.
    void noFramesAfterOpenTriggersTheWatchdog() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy timedOut(&c, &CaptureController::firstFrameTimedOut);
        c.begin();
        QVERIFY(timedOut.wait(CaptureController::FirstFrameTimeoutMs + 2000));
    }

    void framesArrivingCancelTheFirstFrameWatchdog() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy timedOut(&c, &CaptureController::firstFrameTimedOut);
        c.begin();
        src.emitOneFrame();
        QTest::qWait(CaptureController::FirstFrameTimeoutMs + 500);
        QCOMPARE(timedOut.count(), 0);
    }

    void snapshotIsRefusedWhenNoFrameHasArrived() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        QSignalSpy msgs(&c, &CaptureController::status);
        c.begin();
        c.takeSnapshot();
        QTest::qWait(500);
        QCOMPARE(saved.count(), 0);
        QVERIFY(msgs.count() > 0);
    }

    void twoSnapshotsInTheSameSecondBothSurvive() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        c.begin();
        src.emitOneFrame();
        c.takeSnapshot(); src.emitOneFrame();
        c.takeSnapshot(); src.emitOneFrame();

        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, 5000);
        const QString a = saved.at(0).at(0).toString();
        const QString b = saved.at(1).at(0).toString();
        QVERIFY(a != b);
        QVERIFY(QFileInfo(a).size() > 0);
        QVERIFY(QFileInfo(b).size() > 0);
    }

    void stopRecordingWhenNotRecordingIsHarmless() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());
        c.begin();
        c.stopRecording();
        QVERIFY(!c.isRecording());
    }
};

QTEST_MAIN(TestCaptureController)
#include "test_capture_controller.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_capture_controller)` to `tests/CMakeLists.txt`.

Expected: FAIL — `capture/CaptureController.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/capture/CaptureController.h`:

```cpp
#pragma once
#include "core/ICaptureSource.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVideoFrame>

class IRecorder;
class SnapshotWriter;
class QVideoSink;

// Fans frames out to the display sink and, when armed, to the snapshot
// writer and recorder. Owns the watchdogs and the detach path.
//
// It does not know about zoom, and has no way to learn about it: the view
// transform lives entirely in the UI layer, so a capture written here is
// always the full sensor frame (spec 7.2, 9).
class CaptureController : public QObject {
    Q_OBJECT
public:
    static constexpr int FirstFrameTimeoutMs = 3000;
    static constexpr int StallTimeoutMs = 5000;

    CaptureController(ICaptureSource* source, IRecorder* recorder,
                      SnapshotWriter* writer, QString outputDir,
                      QObject* parent = nullptr);

    void begin();
    void takeSnapshot();
    void startRecording();
    void stopRecording();

    bool isRecording() const;
    QVideoSink* displaySink() const { return m_displaySink; }

signals:
    void status(QString message);
    void snapshotSaved(QString path);
    void recordingSaved(QString path);
    void sourceLost(QString message);
    void firstFrameTimedOut();

private:
    void onFrame(const QVideoFrame& frame, qint64 timestampUs);
    void onSourceStopped(StopReason reason, const QString& detail);
    QString reserveName(const QString& extension) const;

    ICaptureSource* m_source;
    IRecorder* m_recorder;
    SnapshotWriter* m_writer;
    QVideoSink* m_displaySink;
    QString m_outputDir;

    QTimer m_firstFrameTimer;
    QTimer m_stallTimer;
    bool m_sawFirstFrame = false;
    bool m_snapshotArmed = false;
    QString m_pendingRecordingPath;
};
```

- [ ] **Step 4: Write the implementation**

`src/capture/CaptureController.cpp`:

```cpp
#include "capture/CaptureController.h"
#include "output/IRecorder.h"
#include "output/SnapshotWriter.h"
#include "storage/CaptureNaming.h"
#include "storage/DiskPolicy.h"
#include "storage/OutputLocation.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QVideoSink>

CaptureController::CaptureController(ICaptureSource* source, IRecorder* recorder,
                                     SnapshotWriter* writer, QString outputDir,
                                     QObject* parent)
    : QObject(parent), m_source(source), m_recorder(recorder), m_writer(writer),
      m_displaySink(new QVideoSink(this)), m_outputDir(std::move(outputDir)) {

    connect(m_source, &ICaptureSource::frameReady, this, &CaptureController::onFrame);
    connect(m_source, &ICaptureSource::stopped, this, &CaptureController::onSourceStopped);

    connect(m_writer, &SnapshotWriter::written, this,
            [this](const QString& path, QSize) {
                emit snapshotSaved(path);
                emit status(tr("Snapshot saved as %1.").arg(QFileInfo(path).fileName()));
            });
    connect(m_writer, &SnapshotWriter::failed, this,
            [this](const QString&, const QString& why) { emit status(why); });

    connect(m_recorder, &IRecorder::finished, this,
            [this](const QString& path, qint64) {
                emit recordingSaved(path);
                emit status(tr("Recording saved as %1.").arg(QFileInfo(path).fileName()));
            });
    connect(m_recorder, &IRecorder::failed, this,
            [this](const QString&, const QString& why) { emit status(why); });

    m_firstFrameTimer.setSingleShot(true);
    m_firstFrameTimer.setInterval(FirstFrameTimeoutMs);
    connect(&m_firstFrameTimer, &QTimer::timeout, this, [this] {
        // open() succeeded but nothing streamed -- the classic shared-hub
        // isochronous bandwidth failure (spec 10.2).
        emit firstFrameTimedOut();
        emit status(tr("No video received from the scope. Nothing was saved. "
                       "Try a direct USB port instead of a hub, or a lower resolution."));
    });

    m_stallTimer.setSingleShot(true);
    m_stallTimer.setInterval(StallTimeoutMs);
    connect(&m_stallTimer, &QTimer::timeout, this, [this] {
        emit status(tr("The video stream stopped. Reconnecting."));
        m_source->stop();
        m_sawFirstFrame = false;
        m_source->start();
        m_firstFrameTimer.start();
    });
}

bool CaptureController::isRecording() const {
    return m_recorder && m_recorder->isRecording();
}

void CaptureController::begin() {
    OutputLocation::ensureExists(m_outputDir);
    m_sawFirstFrame = false;
    if (!m_source->start()) {
        emit status(tr("Could not open the scope. Nothing was saved. "
                       "Check the cable, then reconnect the device."));
        return;
    }
    m_firstFrameTimer.start();
}

QString CaptureController::reserveName(const QString& extension) const {
    const QDir dir(m_outputDir);
    return dir.filePath(CaptureNaming::nextName(
        QDateTime::currentDateTime(), extension,
        [&dir](const QString& name) { return dir.exists(name); }));
}

void CaptureController::onFrame(const QVideoFrame& frame, qint64 timestampUs) {
    if (!m_sawFirstFrame) {
        m_sawFirstFrame = true;
        m_firstFrameTimer.stop();
    }
    m_stallTimer.start();

    m_displaySink->setVideoFrame(frame);          // display: latest wins, drops freely

    if (m_snapshotArmed) {
        m_snapshotArmed = false;
        m_writer->write(frame, timestampUs, reserveName(QStringLiteral("jpg")));
    }

    if (m_recorder->isRecording()) {
        if (DiskPolicy::mustStopRecording(DiskPolicy::freeBytesFor(m_outputDir))) {
            emit status(tr("Storage is nearly full. Stopping and saving the recording."));
            m_recorder->finalizeAndStop();
        } else {
            m_recorder->feed(frame, timestampUs);
        }
    }
}

void CaptureController::takeSnapshot() {
    if (!m_sawFirstFrame) {
        emit status(tr("No video yet, so nothing was saved. "
                       "Wait for the live view, then try again."));
        return;
    }
    if (!DiskPolicy::canSnapshot(DiskPolicy::freeBytesFor(m_outputDir))) {
        emit status(tr("Not enough free storage for a snapshot. Nothing was saved. "
                       "Free some space and try again."));
        return;
    }
    m_snapshotArmed = true;   // the next frame is the one captured
}

void CaptureController::startRecording() {
    if (isRecording()) return;
    if (!DiskPolicy::canStartRecording(DiskPolicy::freeBytesFor(m_outputDir))) {
        emit status(tr("Not enough free storage to record. Nothing was saved. "
                       "Free at least 500 MB and try again."));
        return;
    }
    const QString path = reserveName(QStringLiteral("mp4"));
    const QSize size = m_source->frameSize();
    if (!m_recorder->start(path, size.isEmpty() ? QSize(1920, 1080) : size, 30.0)) {
        emit status(tr("Could not start recording. Nothing was saved."));
        return;
    }
    m_pendingRecordingPath = path;
    emit status(tr("Recording to %1.").arg(QFileInfo(path).fileName()));
}

void CaptureController::stopRecording() {
    if (!isRecording()) return;
    m_recorder->finalizeAndStop();
}

void CaptureController::onSourceStopped(StopReason reason, const QString& detail) {
    Q_UNUSED(detail)
    m_firstFrameTimer.stop();
    m_stallTimer.stop();
    m_snapshotArmed = false;

    const bool wasRecording = isRecording();
    const QString path = m_pendingRecordingPath;

    // Finalize before reporting anything. A truncated MP4 has no moov atom
    // and will not open, so the evidence is simply gone (spec 10.1).
    if (wasRecording) m_recorder->finalizeAndStop();

    if (reason == StopReason::Detached) {
        const QString message = wasRecording
            ? tr("The scope was disconnected. The recording was saved as %1. "
                 "Reconnect the scope to continue.").arg(QFileInfo(path).fileName())
            : tr("The scope was disconnected. Nothing was being recorded. "
                 "Reconnect the scope to continue.");
        emit sourceLost(message);
        emit status(message);
    }
    m_pendingRecordingPath.clear();
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE capture/CaptureController.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_capture_controller --output-on-failure
```

Expected: PASS, 8 test functions.

- [ ] **Step 6: Run the whole suite — nothing earlier may have regressed**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/capture tests/test_capture_controller.cpp src/CMakeLists.txt
git commit -m "feat(capture): controller with watchdogs and detach finalization"
```

---

### Task 10: QtCaptureSource and DeviceRegistry — real desktop hardware

First task that touches a physical camera. Tests here are skipped when no camera is present, so CI stays green; verification is the manual matrix in Task 13.

**Files:**
- Create: `src/device/DeviceRegistry.h`, `src/device/DeviceRegistry.cpp`
- Create: `src/capture/QtCaptureSource.h`, `src/capture/QtCaptureSource.cpp`
- Test: `tests/test_qt_capture_source.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ICaptureSource` (7), `FormatPreference`/`CaptureFormat` (6).
- Produces:
  - `struct ScopeDevice { QString id; QString description; QCameraDevice device; }`
  - `DeviceRegistry::available() -> QList<ScopeDevice>`; signals `attached(ScopeDevice)`, `detached(QString id)`; `void watch()`
  - `QtCaptureSource(QCameraDevice device, QObject* parent = nullptr)` implementing `ICaptureSource`, plus `QList<CaptureFormat> advertisedFormats() const` and `bool selectNextFormat()`

- [ ] **Step 1: Write the failing test**

`tests/test_qt_capture_source.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include "capture/QtCaptureSource.h"
#include "device/DeviceRegistry.h"

class TestQtCaptureSource : public QObject {
    Q_OBJECT
    static bool haveCamera() { return !DeviceRegistry().available().isEmpty(); }

private slots:
    void registryEnumeratesWithoutCrashing() {
        DeviceRegistry registry;
        const auto devices = registry.available();
        for (const auto& d : devices) {
            QVERIFY(!d.id.isEmpty());
            QVERIFY(!d.description.isEmpty());
        }
    }

    void advertisedFormatsAreOrderedBestFirst() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        const auto formats = src.advertisedFormats();
        QVERIFY(!formats.isEmpty());
        QCOMPARE(formats, FormatPreference::order(formats));
    }

    void deliversAFrameFromRealHardware() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        QSignalSpy frames(&src, &ICaptureSource::frameReady);
        QVERIFY(src.start());
        QVERIFY2(frames.wait(5000), "no frame within 5s -- see spec 10.2");
        QVERIFY(!src.frameSize().isEmpty());
        src.stop();
    }

    void stopEmitsRequested() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        QVERIFY(src.start());
        QSignalSpy stopped(&src, &ICaptureSource::stopped);
        src.stop();
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 3000);
        QCOMPARE(stopped.at(0).at(0).value<StopReason>(), StopReason::Requested);
    }

    void selectNextFormatWalksTheFallbackChain() {
        if (!haveCamera()) QSKIP("no camera attached");
        QtCaptureSource src(DeviceRegistry().available().first().device);
        const int count = src.advertisedFormats().size();
        int advanced = 0;
        while (src.selectNextFormat()) ++advanced;
        QCOMPARE(advanced, count - 1);   // exhausts, then reports false
    }
};

QTEST_MAIN(TestQtCaptureSource)
#include "test_qt_capture_source.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_qt_capture_source)` to `tests/CMakeLists.txt`.

Expected: FAIL — `capture/QtCaptureSource.h: No such file or directory`.

- [ ] **Step 3: Write DeviceRegistry**

`src/device/DeviceRegistry.h`:

```cpp
#pragma once
#include <QCameraDevice>
#include <QList>
#include <QObject>
#include <QString>

struct ScopeDevice {
    QString id;
    QString description;
    QCameraDevice device;
};

// Enumerates capture devices and reports attach/detach. On desktop the OS
// class driver has already bound the UVC device, so this is a listing, not
// a claim -- which is why no elevated privileges are needed (spec 4.1).
class DeviceRegistry : public QObject {
    Q_OBJECT
public:
    explicit DeviceRegistry(QObject* parent = nullptr);
    QList<ScopeDevice> available() const;
    void watch();

signals:
    void attached(ScopeDevice device);
    void detached(QString id);

private:
    void refresh();
    QList<QString> m_knownIds;
};
```

`src/device/DeviceRegistry.cpp`:

```cpp
#include "device/DeviceRegistry.h"
#include <QMediaDevices>

DeviceRegistry::DeviceRegistry(QObject* parent) : QObject(parent) {}

QList<ScopeDevice> DeviceRegistry::available() const {
    QList<ScopeDevice> out;
    for (const QCameraDevice& d : QMediaDevices::videoInputs()) {
        if (d.isNull()) continue;
        out.append(ScopeDevice{QString::fromUtf8(d.id()), d.description(), d});
    }
    return out;
}

void DeviceRegistry::watch() {
    auto* devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::videoInputsChanged, this, &DeviceRegistry::refresh);
    refresh();
}

void DeviceRegistry::refresh() {
    const QList<ScopeDevice> now = available();

    QList<QString> currentIds;
    for (const ScopeDevice& d : now) {
        currentIds.append(d.id);
        if (!m_knownIds.contains(d.id)) emit attached(d);
    }
    for (const QString& id : m_knownIds)
        if (!currentIds.contains(id)) emit detached(id);

    m_knownIds = currentIds;
}
```

- [ ] **Step 4: Write QtCaptureSource**

`src/capture/QtCaptureSource.h`:

```cpp
#pragma once
#include "capture/FormatPreference.h"
#include "core/ICaptureSource.h"
#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <memory>

// Desktop implementation: Windows Media Foundation, macOS AVFoundation, and
// Linux V4L2, all via one QCamera. No native code and no driver.
class QtCaptureSource : public ICaptureSource {
    Q_OBJECT
public:
    explicit QtCaptureSource(QCameraDevice device, QObject* parent = nullptr);

    bool start() override;
    void stop() override;
    QSize frameSize() const override { return m_frameSize; }

    QList<CaptureFormat> advertisedFormats() const { return m_formats; }

    // Advances to the next format in the preference chain. Returns false when
    // the chain is exhausted -- the watchdog uses this on a silent open.
    bool selectNextFormat();

private:
    void applySelectedFormat();

    QCameraDevice m_device;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QVideoSink> m_sink;
    QList<CaptureFormat> m_formats;
    QList<QCameraFormat> m_nativeFormats;
    int m_formatIndex = 0;
    QSize m_frameSize;
};
```

`src/capture/QtCaptureSource.cpp`:

```cpp
#include "capture/QtCaptureSource.h"
#include <QDateTime>
#include <algorithm>

QtCaptureSource::QtCaptureSource(QCameraDevice device, QObject* parent)
    : ICaptureSource(parent), m_device(std::move(device)) {

    // Order once through FormatPreference, then rebuild the native list to
    // match. Do NOT try to express the ordering as a pairwise comparator over
    // QCameraFormat -- that is not a strict weak ordering and std::sort is
    // free to do anything with it, including crash.
    const QList<QCameraFormat> native = m_device.videoFormats();

    QList<CaptureFormat> advertised;
    advertised.reserve(native.size());
    for (const QCameraFormat& f : native)
        advertised.append({f.pixelFormat(), f.resolution(), f.maxFrameRate()});

    m_formats = FormatPreference::order(advertised);

    QList<bool> claimed(native.size(), false);
    for (const CaptureFormat& want : m_formats) {
        for (int i = 0; i < native.size(); ++i) {
            if (claimed.at(i)) continue;
            const QCameraFormat& f = native.at(i);
            if (f.pixelFormat() == want.pixelFormat
                && f.resolution() == want.resolution
                && qFuzzyCompare(f.maxFrameRate(), want.maxFrameRate)) {
                m_nativeFormats.append(f);
                claimed[i] = true;
                break;
            }
        }
    }
}

bool QtCaptureSource::start() {
    m_camera  = std::make_unique<QCamera>(m_device);
    m_session = std::make_unique<QMediaCaptureSession>();
    m_sink    = std::make_unique<QVideoSink>();

    m_session->setCamera(m_camera.get());
    m_session->setVideoSink(m_sink.get());
    applySelectedFormat();

    connect(m_sink.get(), &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame& frame) {
                if (!frame.isValid()) return;
                m_frameSize = frame.size();
                const qint64 ts = frame.startTime() >= 0
                    ? frame.startTime()
                    : QDateTime::currentMSecsSinceEpoch() * 1000;
                emit frameReady(frame, ts);
            });

    connect(m_camera.get(), &QCamera::errorOccurred, this,
            [this](QCamera::Error error, const QString& detail) {
                if (error == QCamera::CameraError)
                    emit stopped(StopReason::Detached, detail);
                else
                    emit stopped(StopReason::Error, detail);
            });

    m_camera->start();
    return m_camera->isAvailable();
}

void QtCaptureSource::applySelectedFormat() {
    if (!m_camera || m_formatIndex >= m_nativeFormats.size()) return;
    m_camera->setCameraFormat(m_nativeFormats.at(m_formatIndex));
}

bool QtCaptureSource::selectNextFormat() {
    if (m_formatIndex + 1 >= m_nativeFormats.size()) return false;
    ++m_formatIndex;
    applySelectedFormat();
    return true;
}

void QtCaptureSource::stop() {
    if (!m_camera) return;
    m_camera->stop();
    m_camera.reset();
    m_session.reset();
    m_sink.reset();
    emit stopped(StopReason::Requested, QString());
}
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE
    device/DeviceRegistry.cpp
    capture/QtCaptureSource.cpp)
```

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_qt_capture_source --output-on-failure
```

Expected: PASS or SKIP depending on whether a camera is attached. With the scope connected, `deliversAFrameFromRealHardware` must pass.

- [ ] **Step 6: Commit**

```bash
git add src/device src/capture tests/test_qt_capture_source.cpp src/CMakeLists.txt
git commit -m "feat(capture): desktop QCamera source and device registry"
```

---

### Task 11: ViewTransformModel — expose zoom to QML

**Files:**
- Create: `src/view/ViewTransformModel.h`, `src/view/ViewTransformModel.cpp`
- Test: `tests/test_view_transform_model.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ViewTransform` (3).
- Produces: `ViewTransformModel`, a `QML_ELEMENT` with `Q_PROPERTY`s `zoom`, `contentX`, `contentY`, `contentScale`, `canPan`, invokables `zoomAt(qreal, qreal, qreal)`, `panBy(qreal, qreal)`, `resetToFit()`, `setFrameSize(QSizeF)`, `setViewportSize(QSizeF)`, and signal `changed()`.

`contentScale`/`contentX`/`contentY` are what QML binds to on the `VideoOutput` item, so QML contains no transform arithmetic.

- [ ] **Step 1: Write the failing test**

`tests/test_view_transform_model.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include "view/ViewTransformModel.h"

class TestViewTransformModel : public QObject {
    Q_OBJECT
    static ViewTransformModel* make(QObject* parent) {
        auto* m = new ViewTransformModel(parent);
        m->setFrameSize({1920, 1080});
        m->setViewportSize({1280, 720});
        return m;
    }
private slots:
    void startsAtFit() {
        QObject owner; auto* m = make(&owner);
        QCOMPARE(m->zoom(), 1.0);
        QCOMPARE(m->contentX(), 0.0);
        QCOMPARE(m->contentY(), 0.0);
        QVERIFY(!m->canPan());
    }
    void zoomingEmitsChangedOnce() {
        QObject owner; auto* m = make(&owner);
        QSignalSpy spy(m, &ViewTransformModel::changed);
        m->zoomAt(2.0, 640, 360);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m->zoom(), 2.0);
        QVERIFY(m->canPan());
    }
    void contentScaleGrowsWithZoom() {
        QObject owner; auto* m = make(&owner);
        const qreal before = m->contentScale();
        m->zoomAt(4.0, 640, 360);
        QVERIFY(m->contentScale() > before * 3.5);
    }
    void resetEmitsChangedAndRestoresFit() {
        QObject owner; auto* m = make(&owner);
        m->zoomAt(4.0, 100, 100);
        m->panBy(-50, -20);
        QSignalSpy spy(m, &ViewTransformModel::changed);
        m->resetToFit();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m->zoom(), 1.0);
        QCOMPARE(m->contentX(), 0.0);
    }
    void viewportResizeKeepsZoomValid() {
        QObject owner; auto* m = make(&owner);
        m->zoomAt(8.0, 640, 360);
        m->setViewportSize({400, 400});
        QVERIFY(m->zoom() <= ViewTransform::MaxZoom);
        QVERIFY(m->zoom() >= ViewTransform::MinZoom);
    }
};

QTEST_MAIN(TestViewTransformModel)
#include "test_view_transform_model.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_view_transform_model)` to `tests/CMakeLists.txt`.

Expected: FAIL — `view/ViewTransformModel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/view/ViewTransformModel.h`:

```cpp
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
    QSizeF m_frame{0, 0};
    QSizeF m_viewport{0, 0};
};
```

- [ ] **Step 4: Write the implementation**

`src/view/ViewTransformModel.cpp`:

```cpp
#include "view/ViewTransformModel.h"
#include <algorithm>

qreal ViewTransformModel::contentScale() const {
    if (m_frame.isEmpty() || m_viewport.isEmpty()) return 1.0;
    const qreal fit = std::min(m_viewport.width() / m_frame.width(),
                               m_viewport.height() / m_frame.height());
    return fit * m_t.zoom();
}

qreal ViewTransformModel::contentX() const { return -m_t.pan().x() * contentScale(); }
qreal ViewTransformModel::contentY() const { return -m_t.pan().y() * contentScale(); }

bool ViewTransformModel::canPan() const {
    const QRectF vis = m_t.visibleFrameRect();
    return vis.width() < m_frame.width() - 0.5 || vis.height() < m_frame.height() - 0.5;
}

void ViewTransformModel::setFrameSize(QSizeF size) {
    m_frame = size; m_t.setFrameSize(size); emit changed();
}
void ViewTransformModel::setViewportSize(QSizeF size) {
    m_viewport = size; m_t.setViewportSize(size); emit changed();
}
void ViewTransformModel::zoomAt(qreal factor, qreal focusX, qreal focusY) {
    m_t.zoomAt(factor, {focusX, focusY}); emit changed();
}
void ViewTransformModel::panBy(qreal dx, qreal dy) {
    m_t.panBy({dx, dy}); emit changed();
}
void ViewTransformModel::resetToFit() { m_t.resetToFit(); emit changed(); }
```

- [ ] **Step 5: Add sources and run**

```cmake
target_sources(microscope_core PRIVATE view/ViewTransformModel.cpp)
target_link_libraries(microscope_core PUBLIC Qt6::Qml)
```

Add `Qml` to the root `find_package` components.

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_view_transform_model --output-on-failure
```

Expected: PASS, 5 test functions.

- [ ] **Step 6: Commit**

```bash
git add src/view tests/test_view_transform_model.cpp src/CMakeLists.txt CMakeLists.txt
git commit -m "feat(view): QML-facing transform model"
```

---

### Task 12: QML user interface

**Files:**
- Create: `src/ui/Main.qml`, `src/ui/VideoView.qml`, `src/ui/ControlBar.qml`, `src/ui/StatusBanner.qml`
- Create: `src/AppContext.h`, `src/AppContext.cpp`
- Modify: `src/main.cpp`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `CaptureController` (10), `QtCaptureSource`/`DeviceRegistry` (11), `ViewTransformModel` (12), `QtRecorder` (9), `SnapshotWriter` (8).
- Produces: `AppContext`, a `QML_SINGLETON` exposing `Q_PROPERTY`s `videoSink`, `transform`, `statusText`, `recording`, `hasDevice` and invokables `snapshot()`, `toggleRecording()`, `resetView()`, `openOutputFolder()`.

- [ ] **Step 1: Write AppContext**

`src/AppContext.h`:

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <qqmlintegration.h>
#include <memory>

class CaptureController;
class DeviceRegistry;
class ICaptureSource;
class IRecorder;
class SnapshotWriter;
class ViewTransformModel;
class QVideoSink;

// Wires the pipeline together and exposes exactly what QML needs. Holds no
// logic of its own -- logic lives in microscope_core, where it is tested.
class AppContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVideoSink* videoSink READ videoSink NOTIFY pipelineChanged)
    Q_PROPERTY(ViewTransformModel* transform READ transform CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY pipelineChanged)

public:
    explicit AppContext(QObject* parent = nullptr);
    ~AppContext() override;

    QVideoSink* videoSink() const;
    ViewTransformModel* transform() const { return m_transform; }
    QString statusText() const { return m_statusText; }
    bool recording() const;
    bool hasDevice() const { return m_source != nullptr; }

    Q_INVOKABLE void snapshot();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void openOutputFolder();

signals:
    void pipelineChanged();
    void statusTextChanged();
    void recordingChanged();

private:
    void openFirstAvailableDevice();
    void setStatus(const QString& text);

    DeviceRegistry* m_registry = nullptr;
    ViewTransformModel* m_transform = nullptr;
    SnapshotWriter* m_writer = nullptr;
    std::unique_ptr<IRecorder> m_recorder;
    std::unique_ptr<ICaptureSource> m_source;
    std::unique_ptr<CaptureController> m_controller;
    QString m_statusText;
    QString m_outputDir;
};
```

`src/AppContext.cpp`:

```cpp
#include "AppContext.h"
#include "capture/CaptureController.h"
#include "capture/QtCaptureSource.h"
#include "device/DeviceRegistry.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"
#include "storage/OutputLocation.h"
#include "view/ViewTransformModel.h"
#include <QDesktopServices>
#include <QUrl>
#include <QVideoSink>

AppContext::AppContext(QObject* parent)
    : QObject(parent),
      m_registry(new DeviceRegistry(this)),
      m_transform(new ViewTransformModel(this)),
      m_writer(new SnapshotWriter(this)),
      m_outputDir(OutputLocation::defaultDirectory()) {

    OutputLocation::ensureExists(m_outputDir);

    connect(m_registry, &DeviceRegistry::attached, this, [this](const ScopeDevice&) {
        if (!m_source) openFirstAvailableDevice();
    });
    connect(m_registry, &DeviceRegistry::detached, this, [this](const QString&) {
        emit pipelineChanged();
    });

    m_registry->watch();
    openFirstAvailableDevice();
}

AppContext::~AppContext() = default;

void AppContext::openFirstAvailableDevice() {
    const auto devices = m_registry->available();
    if (devices.isEmpty()) {
        setStatus(tr("No scope detected. Connect the microscope by USB."));
        return;
    }

    auto* source = new QtCaptureSource(devices.first().device);
    m_source.reset(source);
    m_recorder = std::make_unique<QtRecorder>();
    m_controller = std::make_unique<CaptureController>(
        m_source.get(), m_recorder.get(), m_writer, m_outputDir);

    connect(m_controller.get(), &CaptureController::status,
            this, [this](const QString& s) { setStatus(s); });
    connect(m_controller.get(), &CaptureController::sourceLost, this, [this] {
        m_controller.reset(); m_source.reset(); m_recorder.reset();
        emit pipelineChanged();
        emit recordingChanged();
    });
    connect(m_recorder.get(), &IRecorder::finished,
            this, &AppContext::recordingChanged);

    connect(m_source.get(), &ICaptureSource::frameReady, this,
            [this](const QVideoFrame& f, qint64) {
                m_transform->setFrameSize(f.size());
            });

    m_controller->begin();
    setStatus(tr("Connected to %1.").arg(devices.first().description));
    emit pipelineChanged();
}

QVideoSink* AppContext::videoSink() const {
    return m_controller ? m_controller->displaySink() : nullptr;
}

bool AppContext::recording() const {
    return m_controller && m_controller->isRecording();
}

void AppContext::snapshot() {
    if (m_controller) m_controller->takeSnapshot();
}

void AppContext::toggleRecording() {
    if (!m_controller) return;
    if (m_controller->isRecording()) m_controller->stopRecording();
    else m_controller->startRecording();
    emit recordingChanged();
}

void AppContext::resetView() { m_transform->resetToFit(); }

void AppContext::openOutputFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
}

void AppContext::setStatus(const QString& text) {
    if (m_statusText == text) return;
    m_statusText = text;
    emit statusTextChanged();
}
```

- [ ] **Step 2: Write VideoView.qml**

`src/ui/VideoView.qml`:

```qml
import QtQuick
import QtMultimedia
import microscope

Item {
    id: root
    clip: true

    onWidthChanged: AppContext.transform.setViewportSize(Qt.size(width, height))
    onHeightChanged: AppContext.transform.setViewportSize(Qt.size(width, height))

    Rectangle { anchors.fill: parent; color: "#101014" }

    VideoOutput {
        id: output
        anchors.centerIn: parent
        width: root.width
        height: root.height
        fillMode: VideoOutput.PreserveAspectFit

        // All arithmetic lives in ViewTransformModel; these are bindings only.
        scale: AppContext.transform.zoom
        x: (root.width  - width)  / 2 + AppContext.transform.contentX
        y: (root.height - height) / 2 + AppContext.transform.contentY

        Component.onCompleted: if (AppContext.videoSink) output.videoSink = AppContext.videoSink
    }

    Connections {
        target: AppContext
        function onPipelineChanged() {
            if (AppContext.videoSink) output.videoSink = AppContext.videoSink
        }
    }

    PinchHandler {
        target: null
        onActiveScaleChanged: {
            if (activeScale > 0)
                AppContext.transform.zoomAt(activeScale, centroid.position.x, centroid.position.y)
        }
    }

    WheelHandler {
        acceptedModifiers: Qt.NoModifier
        onWheel: (event) => {
            const factor = event.angleDelta.y > 0 ? 1.15 : 1 / 1.15
            AppContext.transform.zoomAt(factor, event.x, event.y)
        }
    }

    DragHandler {
        target: null
        enabled: AppContext.transform.canPan
        property point last: Qt.point(0, 0)
        onActiveChanged: if (active) last = centroid.position
        onCentroidChanged: {
            if (!active) return
            AppContext.transform.panBy(centroid.position.x - last.x,
                                       centroid.position.y - last.y)
            last = centroid.position
        }
    }
}
```

- [ ] **Step 3: Write ControlBar.qml**

`src/ui/ControlBar.qml`:

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import microscope

Pane {
    RowLayout {
        anchors.fill: parent
        spacing: 12

        Button {
            text: qsTr("Snapshot")
            enabled: AppContext.hasDevice
            Layout.preferredHeight: 56       // large enough for gloved hands
            onClicked: AppContext.snapshot()
        }

        Button {
            text: AppContext.recording ? qsTr("Stop recording") : qsTr("Record")
            enabled: AppContext.hasDevice
            Layout.preferredHeight: 56
            highlighted: AppContext.recording
            onClicked: AppContext.toggleRecording()
        }

        Item { Layout.fillWidth: true }

        Label {
            text: qsTr("%1x").arg(AppContext.transform.zoom.toFixed(1))
            font.pixelSize: 18
        }

        Button {
            text: qsTr("Fit")
            Layout.preferredHeight: 56
            onClicked: AppContext.resetView()
        }

        Button {
            text: qsTr("Open folder")
            Layout.preferredHeight: 56
            onClicked: AppContext.openOutputFolder()
        }
    }
}
```

- [ ] **Step 4: Write StatusBanner.qml and Main.qml**

`src/ui/StatusBanner.qml`:

```qml
import QtQuick
import QtQuick.Controls
import microscope

Rectangle {
    visible: AppContext.statusText.length > 0
    implicitHeight: visible ? label.implicitHeight + 20 : 0
    color: AppContext.hasDevice ? "#1d2b1d" : "#2b1d1d"

    Label {
        id: label
        anchors { fill: parent; margins: 10 }
        text: AppContext.statusText
        wrapMode: Text.WordWrap
        color: "#f0f0f0"
    }
}
```

`src/ui/Main.qml`:

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    width: 1100
    height: 760
    visible: true
    title: qsTr("Microscope")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StatusBanner { Layout.fillWidth: true }
        VideoView { Layout.fillWidth: true; Layout.fillHeight: true }
        ControlBar { Layout.fillWidth: true }
    }
}
```

- [ ] **Step 5: Wire up main.cpp and the QML module**

`src/main.cpp`:

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Microscope"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("microscope", "Main");
    if (engine.rootObjects().isEmpty()) return -1;
    return app.exec();
}
```

Replace the executable block in `src/CMakeLists.txt`:

```cmake
qt_add_executable(microscope main.cpp AppContext.cpp)
target_link_libraries(microscope PRIVATE microscope_core Qt6::Quick)

qt_add_qml_module(microscope
    URI microscope
    VERSION 1.0
    QML_FILES ui/Main.qml ui/VideoView.qml ui/ControlBar.qml ui/StatusBanner.qml
    SOURCES
        AppContext.h AppContext.cpp
        view/ViewTransformModel.h view/ViewTransformModel.cpp
)
```

`ViewTransformModel` carries `QML_ELEMENT`, so its header and source must be
listed in `SOURCES` here for the type to register with the `microscope` URI.
It stays compiled into `microscope_core` as well so the unit tests in Task 11
can link it without the QML module.

- [ ] **Step 6: Build and run with the scope attached**

```bash
cmake -B build && cmake --build build
./build/src/microscope
```

Verify by hand, and record the result:

1. Live view appears
2. Pinch or scroll zooms; the point under the cursor stays put
3. Drag pans only when zoomed in; cannot pan past the frame edge
4. "Fit" returns to 1.0x
5. Snapshot writes a JPEG at **full sensor resolution while zoomed in** — confirm with `identify` or file properties, this is the spec's central promise
6. Record for ~10s, stop, and play the MP4 back
7. **Unplug the scope while recording.** The banner must name the saved file, and the file must play

- [ ] **Step 7: Run the full suite and commit**

```bash
ctest --test-dir build --output-on-failure
git add src tests
git commit -m "feat(ui): QML shell with pinch/wheel zoom, capture controls, status banner"
```

---

### Task 13: Zero-admin packaging and the manual test matrix

**Files:**
- Create: `packaging/windows/build-portable.sh`
- Create: `packaging/macos/build-app.sh`
- Create: `packaging/linux/build-appimage.sh`
- Create: `docs/manual-test-matrix.md`
- Create: `README.md`

**Interfaces:**
- Consumes: the built `microscope` target.
- Produces: three installable artifacts, none requiring elevation.

- [ ] **Step 1: Write the Windows portable script**

`packaging/windows/build-portable.sh`:

```bash
#!/usr/bin/env bash
# Portable folder -- unzip and run, no installer, no admin (spec 12).
set -euo pipefail
BUILD=${1:-build}
OUT=dist/microscope-windows

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --config Release

rm -rf "$OUT" && mkdir -p "$OUT"
cp "$BUILD/src/microscope.exe" "$OUT/"
windeployqt --qmldir src/ui --release "$OUT/microscope.exe"

( cd dist && zip -qr microscope-windows.zip microscope-windows )
echo "Built dist/microscope-windows.zip"
```

- [ ] **Step 2: Write the macOS script**

`packaging/macos/build-app.sh`:

```bash
#!/usr/bin/env bash
# Drag-install .app. Gatekeeper BLOCKS unsigned apps outright, so signing
# and notarization are required, not optional (spec 12).
set -euo pipefail
BUILD=${1:-build}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"

APP="$BUILD/src/microscope.app"
macdeployqt "$APP" -qmldir=src/ui

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
  codesign --deep --force --options runtime \
           --entitlements packaging/macos/entitlements.plist \
           --sign "$CODESIGN_IDENTITY" "$APP"
  echo "Signed. Now notarize:"
  echo "  xcrun notarytool submit --wait <zip of $APP>"
else
  echo "WARNING: CODESIGN_IDENTITY unset. Gatekeeper will block this build."
fi
```

Also create `packaging/macos/entitlements.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.device.camera</key>
    <true/>
</dict>
</plist>
```

And `packaging/macos/Info.plist.in`, referenced from CMake. Without
`NSCameraUsageDescription`, macOS terminates the process on first camera
access with no dialog and no error the user can act on:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>            <string>Microscope</string>
    <key>CFBundleExecutable</key>      <string>microscope</string>
    <key>CFBundleIdentifier</key>      <string>com.example.microscope</string>
    <key>CFBundlePackageType</key>     <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>0.1.0</string>
    <key>NSHighResolutionCapable</key> <true/>
    <key>NSCameraUsageDescription</key>
    <string>Microscope needs camera access to show and record the microscope's live view.</string>
</dict>
</plist>
```

Wire it up in `src/CMakeLists.txt`:

```cmake
if(APPLE)
    set_target_properties(microscope PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in)
endif()
```

- [ ] **Step 3: Write the Linux AppImage script**

`packaging/linux/build-appimage.sh`:

```bash
#!/usr/bin/env bash
# AppImage: chmod +x and run. No package manager, no sudo (spec 12).
set -euo pipefail
BUILD=${1:-build}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD"
DESTDIR=AppDir cmake --install "$BUILD"

linuxdeploy --appdir AppDir \
            --plugin qt \
            --output appimage \
            --desktop-file packaging/linux/microscope.desktop \
            --icon-file packaging/linux/microscope.png
echo "Built Microscope-x86_64.AppImage"
```

- [ ] **Step 4: Add the Linux device-permission check**

Add to `CaptureController::begin()`, in the failure branch, a Linux-only hint. This is the one place root is unavoidable, so the app must name the fix (spec §4):

```cpp
#ifdef Q_OS_LINUX
    if (!QFileInfo(QStringLiteral("/dev/video0")).isReadable()) {
        emit status(tr("No permission to read the camera device. Nothing was saved. "
                       "Run: sudo usermod -aG video $USER   then log out and back in."));
        return;
    }
#endif
```

Rebuild and confirm `ctest` still passes.

- [ ] **Step 5: Write the manual test matrix**

`docs/manual-test-matrix.md` — a table with one row per platform (Windows, macOS, Linux) and these columns, each filled in per release:

1. Artifact installs **without admin**
2. Scope enumerates
3. Live view renders
4. Pinch/wheel zoom, focus point stays put
5. Pan clamps at frame edges; no panning at fit
6. **Snapshot is full sensor resolution while zoomed to 8x**
7. Recording plays back at correct speed
8. **Unplug mid-recording: file plays, banner names it**
9. Direct USB port
10. **Through the 6-in-1 hub** — spec §10.2's bandwidth case
11. Output folder opens

Note explicitly: rows 9 and 10 cannot be automated. Real isochronous bandwidth negotiation requires real USB.

- [ ] **Step 6: Write the README**

`README.md`: what the app is, the four features, supported platforms (and that iOS is excluded, with a pointer to spec §3.1), how to build, how to run tests, and where captures are written.

- [ ] **Step 7: Run everything and commit**

```bash
chmod +x packaging/*/*.sh
ctest --test-dir build --output-on-failure
git add packaging docs/manual-test-matrix.md README.md src
git commit -m "build: zero-admin packaging for all three desktop platforms"
```

---

### Task 14: Format fallback chain

Spec §10.2 requires the watchdog to **downgrade the format and retry**, not merely complain. Task 9 wired the watchdog but never walks the chain, so this completes it.

**Files:**
- Modify: `src/core/ICaptureSource.h`
- Modify: `src/capture/QtCaptureSource.h`, `src/capture/QtCaptureSource.cpp`
- Modify: `src/capture/CaptureController.cpp`
- Test: `tests/test_format_fallback.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ICaptureSource::selectNextFormat()` (Task 6), `FakeCaptureSource::setFallbackCount`/`fallbacksUsed` (Task 6).
- Produces: `ICaptureSource::formatDescriptions() -> QStringList` (virtual, default empty); `CaptureController::formatsExhausted(QStringList advertised)` signal.

- [ ] **Step 1: Write the failing test**

`tests/test_format_fallback.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

class TestFormatFallback : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void silentOpenWalksDownTheChain() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);     // opens fine, streams nothing
        src.setFallbackCount(2);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        c.begin();

        // 3 timeouts: initial format, then two fallbacks.
        QVERIFY(exhausted.wait(CaptureController::FirstFrameTimeoutMs * 4 + 3000));
        QCOMPARE(src.fallbacksUsed(), 2);
    }

    void exhaustionReportsWhatTheScopeAdvertised() {
        FakeCaptureSource src;
        src.setDeliverFrames(false);
        src.setFallbackCount(0);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        QSignalSpy msgs(&c, &CaptureController::status);
        c.begin();

        QVERIFY(exhausted.wait(CaptureController::FirstFrameTimeoutMs + 3000));
        QVERIFY(msgs.count() > 0);
        // Must name an action, per spec 10.
        QVERIFY(msgs.last().at(0).toString().contains(QStringLiteral("port")));
    }

    void aFrameArrivingStopsTheFallbackWalk() {
        FakeCaptureSource src;
        src.setFallbackCount(3);
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy exhausted(&c, &CaptureController::formatsExhausted);
        c.begin();
        src.emitOneFrame();
        QTest::qWait(CaptureController::FirstFrameTimeoutMs + 1000);

        QCOMPARE(exhausted.count(), 0);
        QCOMPARE(src.fallbacksUsed(), 0);
    }
};

QTEST_MAIN(TestFormatFallback)
#include "test_format_fallback.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_format_fallback)` to `tests/CMakeLists.txt`.

Expected: FAIL — `CaptureController::formatsExhausted` does not exist.

- [ ] **Step 3: Add formatDescriptions to the interface**

In `src/core/ICaptureSource.h`, alongside `selectNextFormat`:

```cpp
    // Human-readable list of what the device advertised, for the "all formats
    // failed" message. Reporting the list is what makes a silent scope
    // diagnosable from the field (spec 10.3).
    virtual QStringList formatDescriptions() const { return {}; }
```

Add `#include <QStringList>`.

- [ ] **Step 4: Implement it in QtCaptureSource**

In `src/capture/QtCaptureSource.h` add `QStringList formatDescriptions() const override;`, and in the `.cpp`:

```cpp
QStringList QtCaptureSource::formatDescriptions() const {
    QStringList out;
    out.reserve(m_formats.size());
    for (const CaptureFormat& f : m_formats) {
        out.append(QStringLiteral("%1x%2 @ %3fps")
                       .arg(f.resolution.width())
                       .arg(f.resolution.height())
                       .arg(f.maxFrameRate, 0, 'f', 0));
    }
    return out;
}
```

- [ ] **Step 5: Wire the watchdog to the chain**

In `src/capture/CaptureController.h` add the signal `void formatsExhausted(QStringList advertised);`.

In `src/capture/CaptureController.cpp`, replace the `m_firstFrameTimer` timeout lambda:

```cpp
    connect(&m_firstFrameTimer, &QTimer::timeout, this, [this] {
        // open() succeeded but nothing streamed -- the classic shared-hub
        // isochronous bandwidth failure (spec 10.2). Walk down the preference
        // chain before giving up; complaining without retrying is not what
        // the spec asks for.
        if (m_source->selectNextFormat()) {
            emit status(tr("No video at this resolution. Trying a lower one."));
            m_source->stop();
            m_sawFirstFrame = false;
            m_source->start();
            m_firstFrameTimer.start();
            return;
        }

        const QStringList advertised = m_source->formatDescriptions();
        emit firstFrameTimedOut();
        emit formatsExhausted(advertised);
        emit status(tr("No video received from the scope at any resolution. "
                       "Nothing was saved. Try a direct USB port instead of a hub. "
                       "The scope offered: %1")
                        .arg(advertised.isEmpty() ? tr("no formats")
                                                  : advertised.join(QStringLiteral(", "))));
    });
```

Note: `m_source->stop()` emits `stopped(Requested, ...)`, which `onSourceStopped` ignores for anything but `Detached`, so the restart is safe.

- [ ] **Step 6: Run the tests**

```bash
cmake -B build && cmake --build build && ctest --test-dir build -R test_format_fallback --output-on-failure
```

Expected: PASS, 3 test functions. Then the full suite:

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src tests/test_format_fallback.cpp tests/CMakeLists.txt
git commit -m "feat(capture): walk the format preference chain on a silent open"
```

---

### Task 15: Remaining failure modes from spec §10.3

Four rows of the spec's failure table have code but no test, or a message that discards the information the user needs.

**Files:**
- Modify: `src/capture/CaptureController.cpp`
- Modify: `src/AppContext.h`, `src/AppContext.cpp`
- Modify: `src/ui/StatusBanner.qml`
- Test: `tests/test_failure_modes.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 6–12.
- Produces: `AppContext::openCameraPrivacySettings()` invokable; `AppContext::cameraAccessDenied` property.

- [ ] **Step 1: Write the failing test**

`tests/test_failure_modes.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "capture/CaptureController.h"
#include "core/FakeCaptureSource.h"
#include "output/QtRecorder.h"
#include "output/SnapshotWriter.h"

class TestFailureModes : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    // Spec 10.3: "Detach mid-snapshot -> discard partial write; no orphan file"
    void detachBeforeTheArmedFrameLeavesNoFile() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy saved(&c, &CaptureController::snapshotSaved);
        c.begin();
        src.emitOneFrame();
        c.takeSnapshot();        // armed, waiting for the next frame
        src.injectDetach();      // ...which never comes
        QTest::qWait(500);

        QCOMPARE(saved.count(), 0);
        QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral("*.jpg")},
                                              QDir::Files).size(), 0);
    }

    // Spec 10.3: "Stream stalls later (no frame for 5s) -> one silent reopen"
    void stalledStreamIsReopenedOnce() {
        FakeCaptureSource src; src.setFrameSize({320, 240});
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy msgs(&c, &CaptureController::status);
        c.begin();
        src.emitOneFrame();      // starts the stall timer
        // then nothing -- the stall watchdog must fire
        QTRY_VERIFY_WITH_TIMEOUT(
            msgs.count() > 0 && msgs.last().at(0).toString()
                .contains(QStringLiteral("stopped")),
            CaptureController::StallTimeoutMs + 3000);
    }

    // Spec 10.3: "Device claimed by another app -> name the conflict, not
    // 'failed to open'". The detail string is the only place that
    // information exists, so discarding it makes the row unimplementable.
    void errorDetailReachesTheUserMessage() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy lost(&c, &CaptureController::sourceLost);
        c.begin();
        src.emitOneFrame();
        emit src.stopped(StopReason::Detached,
                         QStringLiteral("device in use by OtherApp"));

        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
        QVERIFY(lost.at(0).at(0).toString().contains(QStringLiteral("OtherApp")));
    }

    void detachWithNoRecordingSaysNothingWasBeingRecorded() {
        FakeCaptureSource src;
        QtRecorder rec; SnapshotWriter writer;
        CaptureController c(&src, &rec, &writer, m_dir.path());

        QSignalSpy lost(&c, &CaptureController::sourceLost);
        c.begin();
        src.emitOneFrame();
        src.injectDetach();

        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
        const QString msg = lost.at(0).at(0).toString();
        QVERIFY(msg.contains(QStringLiteral("disconnected")));
        QVERIFY(msg.contains(QStringLiteral("Reconnect")));
    }
};

QTEST_MAIN(TestFailureModes)
#include "test_failure_modes.moc"
```

The third test emits `stopped` directly, which requires the signal to be public — it already is, since `signals:` members are public in Qt.

- [ ] **Step 2: Register and run to verify it fails**

Add `microscope_test(test_failure_modes)` to `tests/CMakeLists.txt`.

Expected: `errorDetailReachesTheUserMessage` FAILS — `onSourceStopped` currently discards `detail` via `Q_UNUSED`.

- [ ] **Step 3: Stop discarding the error detail**

In `src/capture/CaptureController.cpp`, replace `onSourceStopped`'s body from `Q_UNUSED(detail)` through the end:

```cpp
void CaptureController::onSourceStopped(StopReason reason, const QString& detail) {
    m_firstFrameTimer.stop();
    m_stallTimer.stop();
    m_snapshotArmed = false;   // an armed snapshot never fires; no orphan file

    const bool wasRecording = isRecording();
    const QString path = m_pendingRecordingPath;

    // Finalize before reporting anything. A truncated MP4 has no moov atom
    // and will not open, so the evidence is simply gone (spec 10.1).
    if (wasRecording) m_recorder->finalizeAndStop();

    if (reason == StopReason::Detached || reason == StopReason::Error) {
        QString message = wasRecording
            ? tr("The scope stopped. The recording was saved as %1. "
                 "Reconnect the scope to continue.").arg(QFileInfo(path).fileName())
            : tr("The scope stopped. Nothing was being recorded. "
                 "Reconnect the scope to continue.");

        // The driver's detail names the real cause -- another app holding the
        // device, a bandwidth failure, a vanished node. Dropping it is what
        // turns a diagnosable fault into "failed to open" (spec 10.3).
        if (!detail.isEmpty())
            message += tr(" Reported cause: %1.").arg(detail);

        emit sourceLost(message);
        emit status(message);
    }
    m_pendingRecordingPath.clear();
}
```

- [ ] **Step 4: Keep the "disconnected" wording the earlier tests assert**

Task 9's `detachMidRecordingFinalizesAndNamesTheFile` and this task's `detachWithNoRecordingSaysNothingWasBeingRecorded` both expect "disconnected". Use it for `Detached` and "stopped" for `Error`:

```cpp
    const QString what = (reason == StopReason::Detached)
        ? tr("The scope was disconnected.")
        : tr("The scope stopped.");
```

Build the message from `what` plus the recording clause plus "Reconnect the scope to continue." Re-run Task 9's suite to confirm both still pass.

- [ ] **Step 5: Add the macOS camera-privacy deep link**

Spec §10.3 requires a deep-link when camera access is denied. In `src/AppContext.h`:

```cpp
    Q_PROPERTY(bool cameraAccessDenied READ cameraAccessDenied NOTIFY pipelineChanged)
public:
    bool cameraAccessDenied() const { return m_cameraAccessDenied; }
    Q_INVOKABLE void openCameraPrivacySettings();
private:
    bool m_cameraAccessDenied = false;
```

In `src/AppContext.cpp`:

```cpp
void AppContext::openCameraPrivacySettings() {
#ifdef Q_OS_MACOS
    QDesktopServices::openUrl(QUrl(QStringLiteral(
        "x-apple.systempreferences:com.apple.preference.security?Privacy_Camera")));
#elif defined(Q_OS_WIN)
    QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:privacy-webcam")));
#else
    setStatus(tr("Run: sudo usermod -aG video $USER   then log out and back in."));
#endif
}
```

Set `m_cameraAccessDenied = true` in `openFirstAvailableDevice()` when the device list is empty **and** `QMediaDevices::videoInputs()` was non-empty at startup — the signature of a permission block rather than an absent device.

- [ ] **Step 6: Surface it in the UI**

In `src/ui/StatusBanner.qml`, add inside the `Rectangle`, anchored right:

```qml
    Button {
        visible: AppContext.cameraAccessDenied
        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 10 }
        text: qsTr("Open camera settings")
        onClicked: AppContext.openCameraPrivacySettings()
    }
```

Add `import QtQuick.Controls` if not already present, and give `label` a right anchor margin so the text does not run under the button.

- [ ] **Step 7: Run the full suite and commit**

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: every test passes, including Task 9's.

```bash
git add src tests/test_failure_modes.cpp tests/CMakeLists.txt
git commit -m "feat: complete spec 10.3 failure coverage and preserve error detail"
```

