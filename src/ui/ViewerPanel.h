#pragma once

#include <QElapsedTimer>
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

// Timeline viewer with transport controls and real-time video playback.
// Playback is clocked by wall time; frames that cannot be rendered in time are
// skipped (and counted) rather than slowing playback down.
class ViewerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewerPanel(QWidget* parent = nullptr);
    ~ViewerPanel() override;

    void setSession(EditorSession* session);
    FrameIndex position() const { return position_; }
    bool isPlaying() const;
    int droppedFrames() const { return dropped_; }
    const QImage& currentImage() const;

public slots:
    void setPosition(FrameIndex frame);
    void togglePlay();
    void stop();
    void step(int frames);
    void goToStart();
    void goToEnd();
    // Re-renders the current frame (after edits or media changes).
    void refresh();

signals:
    void positionChanged(FrameIndex frame);
    void errorOccurred(const QString& message);

private:
    void tick();
    void renderCurrent();
    void updateLabel();

    EditorSession* session_ = nullptr;
    std::unique_ptr<render::FrameCompositor> compositor_;
    FrameView* view_ = nullptr;
    QLabel* timecode_ = nullptr;
    QToolButton* playButton_ = nullptr;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    FrameIndex position_ = 0;
    FrameIndex playStart_ = 0;
    int dropped_ = 0;
    QString lastError_;
};

}  // namespace up::ui
