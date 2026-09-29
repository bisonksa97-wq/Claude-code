#pragma once

#include <QString>
#include <QWidget>
#include <optional>
#include <string>
#include <vector>

#include "core/Rational.h"

class QScrollBar;

namespace up {
class EditorSession;
class MediaAssets;
struct Clip;
struct Track;
namespace media {
struct WaveformPeaks;
}
}  // namespace up

namespace up::ui {

// Interactive timeline editor. Presents the active timeline of an EditorSession
// and turns mouse/drag input into application-service calls (never mutating the
// model directly). Supports: dropping media (overwrite; Ctrl = insert), selecting,
// moving clips between tracks, trimming edges (Shift = ripple), scrubbing, track
// toggles, zoom (Ctrl+wheel) and snapping to edit points and the playhead.
class TimelineView : public QWidget {
    Q_OBJECT
public:
    explicit TimelineView(QWidget* parent = nullptr);

    void setSession(EditorSession* session);
    // Source of clip thumbnails and audio waveforms (may be null).
    void setAssets(MediaAssets* assets) {
        assets_ = assets;
        update();
    }
    FrameIndex playhead() const { return playhead_; }
    QString selectedClipId() const { return QString::fromStdString(selected_); }
    void selectClip(const QString& clipId);

    double pixelsPerFrame() const { return pixelsPerFrame_; }
    void setPixelsPerFrame(double ppf);
    FrameIndex scrollFrame() const { return scroll_; }
    void setScrollFrame(FrameIndex frame);
    FrameIndex contentFrames() const;
    FrameIndex visibleFrames() const;

    // Geometry helpers (public for tests and accessibility tooling).
    int xForFrame(FrameIndex frame) const;
    FrameIndex frameForX(int x) const;
    QRect clipRect(const std::string& clipId) const;
    int rowTop(int row) const;
    std::vector<std::string> rowTrackIds() const;

public slots:
    void setPlayhead(FrameIndex frame);
    void zoomIn();
    void zoomOut();
    void zoomToFit();

signals:
    void playheadMoved(FrameIndex frame);
    void selectionChanged(const QString& clipId);
    void viewChanged();
    void errorOccurred(const QString& message);
    void statusMessage(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class DragKind { None, Scrub, Move, TrimIn, TrimOut };
    enum class Zone { Body, In, Out };
    struct Hit {
        std::string clipId;
        Zone zone = Zone::Body;
    };

    int rowAt(int y) const;
    std::optional<Hit> hitTest(const QPoint& pos) const;
    FrameIndex snap(FrameIndex frame, const std::vector<std::string>& exclude) const;
    bool handleHeaderClick(const QPoint& pos);
    void report(const QString& message);
    void drawTrackHeader(QPainter& p, const Track& track, int row) const;
    void drawClip(QPainter& p, const Clip& clip, const QRect& r, bool isVideo) const;
    void drawWaveform(QPainter& p, const Clip& clip, const QRect& r, const media::WaveformPeaks& peaks) const;
    QRect toggleRect(int row, int index) const;
    // Source-patch box at the left of a track header (filled = this track receives the source).
    QRect targetRect(int row) const;

    EditorSession* session_ = nullptr;
    MediaAssets* assets_ = nullptr;
    FrameIndex playhead_ = 0;
    FrameIndex scroll_ = 0;
    double pixelsPerFrame_ = 4.0;
    std::string selected_;

    DragKind drag_ = DragKind::None;
    std::string dragClip_;
    QPoint pressPos_;
    FrameIndex pressFrame_ = 0;
    FrameIndex dragDelta_ = 0;
    int dragRow_ = -1;
    bool dragRipple_ = false;

    std::optional<FrameIndex> dropFrame_;
    int dropRow_ = -1;
};

// TimelineView plus a horizontal scroll bar kept in sync with it.
class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel(QWidget* parent = nullptr);
    TimelineView* view() const { return view_; }

private:
    void syncScrollBar();
    TimelineView* view_ = nullptr;
    QScrollBar* scrollBar_ = nullptr;
};

}  // namespace up::ui
