#include "ui/ViewerPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/EditorSession.h"
#include "core/Timecode.h"
#include "render/FrameCompositor.h"
#include "ui/Theme.h"

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
    updateLabel();
}

ViewerPanel::~ViewerPanel() = default;

void ViewerPanel::setSession(EditorSession* session) {
    stop();
    session_ = session;
    compositor_.reset();
    if (session_) compositor_ = std::make_unique<render::FrameCompositor>(render::resolverFor(session_->project()), 8);
    position_ = 0;
    refresh();
}

bool ViewerPanel::isPlaying() const { return timer_->isActive(); }
const QImage& ViewerPanel::currentImage() const { return view_->image(); }

void ViewerPanel::setPosition(FrameIndex frame) {
    frame = std::max<FrameIndex>(0, frame);
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
    if (position_ >= session_->timeline().duration()) position_ = 0;
    playStart_ = position_;
    dropped_ = 0;
    clock_.start();
    const double fps = session_->timeline().frameRate.toDouble();
    timer_->start(std::max(1, static_cast<int>(1000.0 / fps / 2)));
    playButton_->setText("❚❚");
}

void ViewerPanel::stop() {
    timer_->stop();
    if (playButton_) playButton_->setText("▶");
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
    renderCurrent();
    updateLabel();
}

void ViewerPanel::tick() {
    if (!session_) return;
    const FrameRate rate = session_->timeline().frameRate;
    const FrameIndex target = playStart_ + secondsToFrames(static_cast<double>(clock_.elapsed()) / 1000.0, rate);
    const FrameIndex end = session_->timeline().duration();
    if (target >= end) {
        stop();
        setPosition(std::max<FrameIndex>(0, end - 1));
        return;
    }
    if (target == position_) return;
    if (target > position_ + 1) dropped_ += static_cast<int>(target - position_ - 1);
    setPosition(target);
}

void ViewerPanel::renderCurrent() {
    if (!session_ || !compositor_) {
        view_->setImage({});
        return;
    }
    const Timeline& tl = session_->timeline();
    // Preview at the display size (bounded by the timeline resolution) to keep playback fast.
    const QSize fit = QSize(tl.width, tl.height).scaled(view_->size().boundedTo(QSize(tl.width, tl.height)), Qt::KeepAspectRatio);
    const int w = std::max(2, fit.width() & ~1);
    const int h = std::max(2, fit.height() & ~1);
    auto frame = compositor_->render(tl, position_, w, h);
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
