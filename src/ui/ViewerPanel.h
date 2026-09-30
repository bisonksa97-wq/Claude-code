#pragma once

#include <QImage>
#include <QWidget>
#include <memory>
#include <optional>
#include <string>

#include "core/Rational.h"
#include "render/AudioMixer.h"

class QLabel;
class QTimer;
class QToolButton;

namespace up {
class Project;
class Timeline;
namespace render {
class FrameCompositor;
}
namespace playback {
class AudioOutput;
class PlaybackEngine;
}  // namespace playback
}  // namespace up

namespace up::ui {

// Displays rendered frames (via the render engine's compositor, never by touching
// decoders directly).
class FrameView : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    void setImage(QImage image);
    const QImage& image() const { return image_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
};

// Horizontal position bar: shows the playhead and the in/out marks, click or drag to seek.
class ScrubBar : public QWidget {
    Q_OBJECT
public:
    explicit ScrubBar(QWidget* parent = nullptr);
    void setRange(FrameIndex duration);
    void setPosition(FrameIndex frame);
    void setMarks(std::optional<FrameIndex> in, std::optional<FrameIndex> out);
    QSize sizeHint() const override;

signals:
    void seekRequested(FrameIndex frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    FrameIndex frameAt(int x) const;
    int xAt(FrameIndex frame) const;
    FrameIndex duration_ = 0;
    FrameIndex position_ = 0;
    std::optional<FrameIndex> markIn_;
    std::optional<FrameIndex> markOut_;
};

// Viewer with transport controls, marks and real-time playback. Used both as the
// program monitor (the edited timeline) and the source monitor (one media item).
// It shows any (project, timeline) pair and never edits: mark and edit buttons
// emit requests that the window routes to the application services.
// Playback runs in playback::PlaybackEngine (audio + video workers, audio as the
// master clock when a device exists); this panel only displays what it produces.
// While stopped, frames are rendered on demand at the playhead.
class ViewerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewerPanel(QWidget* parent = nullptr);
    ~ViewerPanel() override;

    // The project/timeline to show; the project must outlive the viewer's use of it.
    void setSource(const Project* project, std::string timelineId);
    void setTitle(const QString& title);
    // Highlights the viewer that keyboard transport/mark commands currently address.
    void setActive(bool active);
    bool isActive() const { return active_; }
    // Marks to display (owned by the model; see markInRequested etc.).
    void setMarks(std::optional<FrameIndex> in, std::optional<FrameIndex> out);
    FrameIndex duration() const;
    // Replaces the audio device (tests inject fakes; nullptr = silent, wall-clock playback).
    void setAudioOutput(std::shared_ptr<playback::AudioOutput> output);
    FrameIndex position() const { return position_; }
    bool isPlaying() const;
    int droppedFrames() const;
    bool playingWithAudio() const;
    const QImage& currentImage() const;
    // Levels of what is being heard (nullopt when not playing).
    std::optional<render::MixMeters> meters() const;

public slots:
    void setPosition(FrameIndex frame);
    void togglePlay();
    void stop();
    void step(int frames);
    void goToStart();
    void goToEnd();
    // Re-renders the current frame after edits or media changes; restarts playback
    // from the current position so the change is heard and seen immediately.
    void refresh();

signals:
    void positionChanged(FrameIndex frame);
    void errorOccurred(const QString& message);
    void activated();
    void markInRequested(FrameIndex frame);
    void markOutRequested(FrameIndex frame);
    void clearMarksRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    const Timeline* timeline() const;
    void tick();
    void renderCurrent();
    void updateLabel();
    QSize previewSize() const;

    const Project* project_ = nullptr;
    std::string timelineId_;
    bool active_ = false;
    QLabel* title_ = nullptr;
    ScrubBar* scrub_ = nullptr;
    std::unique_ptr<render::FrameCompositor> compositor_;
    FrameView* view_ = nullptr;
    QLabel* timecode_ = nullptr;
    QLabel* playbackInfo_ = nullptr;
    std::unique_ptr<playback::PlaybackEngine> engine_;
    QToolButton* playButton_ = nullptr;
    QTimer* timer_ = nullptr;
    FrameIndex position_ = 0;
    QString lastError_;
};

}  // namespace up::ui
