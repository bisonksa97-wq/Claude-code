#include "ui/ViewerPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/EditorSession.h"
#include "core/Timecode.h"
#include "playback/PlaybackEngine.h"
#include "render/FrameCompositor.h"
#include "ui/Theme.h"
#ifdef UP_HAVE_QT_MULTIMEDIA
#include "ui/QtAudioOutput.h"
#endif

namespace up::ui {

void FrameView::setImage(QImage image) {
    image_ = std::move(image);
    update();
}

void FrameView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (image_.isNull()) return;
    QSize size = image_.size().scaled(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, image_);
}

ViewerPanel::ViewerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    view_ = new FrameView(this);
    view_->setMinimumSize(320, 180);
    view_->setAccessibleName(tr("Timeline viewer"));
    layout->addWidget(view_, 1);

    auto* bar = new QHBoxLayout;
    auto makeButton = [&](const QString& text, const QString& tip, auto slot) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setFocusPolicy(Qt::TabFocus);
        connect(b, &QToolButton::clicked, this, slot);
        bar->addWidget(b);
        return b;
    };
    makeButton("|◀", tr("Go to start (Home)"), &ViewerPanel::goToStart);
    makeButton("◁", tr("Step back one frame (Left)"), [this] { step(-1); });
    playButton_ = makeButton("▶", tr("Play / pause (Space)"), &ViewerPanel::togglePlay);
    makeButton("▷", tr("Step forward one frame (Right)"), [this] { step(1); });
    makeButton("▶|", tr("Go to end (End)"), &ViewerPanel::goToEnd);
    bar->addStretch(1);
    playbackInfo_ = new QLabel(this);
    playbackInfo_->setAccessibleName(tr("Playback status"));
    bar->addWidget(playbackInfo_);
    bar->addSpacing(12);
    timecode_ = new QLabel(this);
    QFont mono("monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSizeF(font().pointSizeF() * 1.3);
    timecode_->setFont(mono);
    timecode_->setAccessibleName(tr("Playhead timecode"));
    bar->addWidget(timecode_);
    layout->addLayout(bar);

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &ViewerPanel::tick);

    std::shared_ptr<playback::AudioOutput> audio;
#ifdef UP_HAVE_QT_MULTIMEDIA
    audio = std::make_shared<QtAudioOutput>();
#endif
    engine_ = std::make_unique<playback::PlaybackEngine>(audio);
    updateLabel();
}

ViewerPanel::~ViewerPanel() { engine_->stop(); }

void ViewerPanel::setAudioOutput(std::shared_ptr<playback::AudioOutput> output) {
    stop();
    engine_ = std::make_unique<playback::PlaybackEngine>(std::move(output));
}

int ViewerPanel::droppedFrames() const { return static_cast<int>(engine_->stats().droppedFrames); }
bool ViewerPanel::playingWithAudio() const { return engine_->isRunning() && engine_->usingAudioClock(); }

void ViewerPanel::setSession(EditorSession* session) {
    stop();
    session_ = session;
    compositor_.reset();
    if (session_) compositor_ = std::make_unique<render::FrameCompositor>(render::resolverFor(session_->project()), 8);
    position_ = 0;
    refresh();
}

bool ViewerPanel::isPlaying() const { return engine_->isRunning(); }
const QImage& ViewerPanel::currentImage() const { return view_->image(); }

void ViewerPanel::setPosition(FrameIndex frame) {
    frame = std::max<FrameIndex>(0, frame);
    if (isPlaying()) stop();  // scrubbing or stepping interrupts playback
    if (frame == position_ && !view_->image().isNull()) return;
    position_ = frame;
    renderCurrent();
    updateLabel();
    emit positionChanged(position_);
}

void ViewerPanel::togglePlay() {
    if (!session_) return;
    if (isPlaying()) {
        stop();
        return;
    }
    const Timeline& tl = session_->timeline();
    if (tl.duration() == 0) return;
    if (position_ >= tl.duration() - 1) position_ = 0;
    const QSize size = previewSize();
    Status s = engine_->start(session_->project(), tl.id, position_, size.width(), size.height());
    if (!s.ok()) {
        emit errorOccurred(QString::fromStdString(s.error().toString()));
        return;
    }
    // Poll well above the frame rate; the engine decides which frame is due.
    timer_->start(std::max(1, static_cast<int>(1000.0 / tl.frameRate.toDouble() / 3)));
    playButton_->setText("❚❚");
    playbackInfo_->setText(engine_->usingAudioClock() ? tr("Audio") : tr("No audio output"));
    playbackInfo_->setToolTip(engine_->usingAudioClock() ? tr("Playback is synchronised to the audio device.")
                                                         : tr("No audio device is available; playing silently using the system clock."));
}

void ViewerPanel::stop() {
    const bool wasPlaying = engine_ && engine_->isRunning();
    if (engine_) engine_->stop();
    timer_->stop();
    if (playButton_) playButton_->setText("▶");
    if (wasPlaying) {
        renderCurrent();  // show the exact frame at the stop position
        updateLabel();
    }
}

void ViewerPanel::step(int frames) {
    stop();
    setPosition(position_ + frames);
}

void ViewerPanel::goToStart() {
    stop();
    setPosition(0);
}

void ViewerPanel::goToEnd() {
    stop();
    if (session_) setPosition(std::max<FrameIndex>(0, session_->timeline().duration() - 1));
}

void ViewerPanel::refresh() {
    if (isPlaying()) {
        stop();
        togglePlay();
        return;
    }
    renderCurrent();
    updateLabel();
}

void ViewerPanel::tick() {
    if (!session_ || !isPlaying()) return;
    if (auto frame = engine_->frameForDisplay()) {
        const VideoFrame& f = frame->image;
        view_->setImage(QImage(f.pixels.data(), f.width, f.height, f.width * 4, QImage::Format_RGBA8888).copy());
    }
    const FrameIndex end = engine_->endFrame();
    const FrameIndex now = std::min(engine_->position(), std::max<FrameIndex>(0, end - 1));
    if (now != position_) {
        position_ = now;
        updateLabel();
        emit positionChanged(position_);
    }
    const auto stats = engine_->stats();
    if (stats.droppedFrames > 0) playbackInfo_->setText(tr("%1 · %2 dropped").arg(engine_->usingAudioClock() ? tr("Audio") : tr("No audio output")).arg(stats.droppedFrames));
    if (Status err = engine_->lastError(); !err.ok()) {
        stop();
        emit errorOccurred(QString::fromStdString(err.error().toString()));
        return;
    }
    if (engine_->finished()) stop();
}

QSize ViewerPanel::previewSize() const {
    const Timeline& tl = session_->timeline();
    // Preview at the display size (bounded by the timeline resolution) to keep playback fast.
    const QSize fit = QSize(tl.width, tl.height).scaled(view_->size().boundedTo(QSize(tl.width, tl.height)), Qt::KeepAspectRatio);
    return {std::max(2, fit.width() & ~1), std::max(2, fit.height() & ~1)};
}

void ViewerPanel::renderCurrent() {
    if (!session_ || !compositor_) {
        view_->setImage({});
        return;
    }
    const QSize size = previewSize();
    auto frame = compositor_->render(session_->timeline(), position_, size.width(), size.height());
    if (!frame.ok()) {
        const QString message = QString::fromStdString(frame.error().toString());
        if (message != lastError_) emit errorOccurred(message);
        lastError_ = message;
        return;
    }
    lastError_.clear();
    const VideoFrame& f = frame.value();
    view_->setImage(QImage(f.pixels.data(), f.width, f.height, f.width * 4, QImage::Format_RGBA8888).copy());
}

void ViewerPanel::updateLabel() {
    if (!session_) {
        timecode_->setText("--:--:--:--");
        return;
    }
    const FrameRate rate = session_->timeline().frameRate;
    timecode_->setText(QString::fromStdString(formatTimecode(position_, rate)) + " / " +
                       QString::fromStdString(formatTimecode(session_->timeline().duration(), rate)));
}

}  // namespace up::ui
