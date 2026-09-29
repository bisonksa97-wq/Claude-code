#pragma once

#include <QImage>
#include <QWidget>
#include <memory>

#include "core/Rational.h"

class QLabel;
class QTimer;
class QToolButton;

namespace up {
class EditorSession;
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

// Timeline viewer with transport controls and real-time playback.
// Playback runs in playback::PlaybackEngine (audio + video workers, audio as the
// master clock when a device exists); this panel only displays what it produces.
// While stopped, frames are rendered on demand at the playhead.
class ViewerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewerPanel(QWidget* parent = nullptr);
    ~ViewerPanel() override;

    void setSession(EditorSession* session);
    // Replaces the audio device (tests inject fakes; nullptr = silent, wall-clock playback).
    void setAudioOutput(std::shared_ptr<playback::AudioOutput> output);
    FrameIndex position() const { return position_; }
    bool isPlaying() const;
    int droppedFrames() const;
    bool playingWithAudio() const;
    const QImage& currentImage() const;

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

private:
    void tick();
    void renderCurrent();
    void updateLabel();
    QSize previewSize() const;

    EditorSession* session_ = nullptr;
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
