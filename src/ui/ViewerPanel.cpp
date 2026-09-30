#include "ui/ViewerPanel.h"

#include <QComboBox>
#include <cstring>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <QEvent>
#include <QMouseEvent>

#include "project/Project.h"
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
    updateDisplayed();
}

void FrameView::setOverlay(render::ViewerOverlay overlay) {
    overlay_ = overlay;
    updateDisplayed();
}

void FrameView::updateDisplayed() {
    displayed_ = {};
    if (overlay_ != render::ViewerOverlay::None && !image_.isNull()) {
        const QImage rgba = image_.convertToFormat(QImage::Format_RGBA8888);
        VideoFrame frame(rgba.width(), rgba.height());
        for (int y = 0; y < rgba.height(); ++y)
            std::memcpy(frame.row(y), rgba.constScanLine(y), static_cast<std::size_t>(rgba.width()) * 4);
        render::applyOverlay(frame, overlay_);
        displayed_ = QImage(frame.pixels.data(), frame.width, frame.height, frame.width * 4, QImage::Format_RGBA8888).copy();
    }
    update();
}

void FrameView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    const QImage& shown = displayedImage();
    if (shown.isNull()) return;
    QSize size = shown.size().scaled(this->size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, shown);
}

ViewerPanel::ViewerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    title_ = new QLabel(this);
    layout->addWidget(title_);
    view_ = new FrameView(this);
    view_->setMinimumSize(320, 180);
    view_->setAccessibleName(tr("Timeline viewer"));
    layout->addWidget(view_, 1);
    scrub_ = new ScrubBar(this);
    scrub_->setAccessibleName(tr("Position"));
    connect(scrub_, &ScrubBar::seekRequested, this, &ViewerPanel::setPosition);
    layout->addWidget(scrub_);

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
    bar->addSpacing(12);
    makeButton("{", tr("Mark in (I)"), [this] { emit markInRequested(position_); });
    makeButton("}", tr("Mark out (O)"), [this] { emit markOutRequested(position_ + 1); });
    makeButton("{×}", tr("Clear marks (Alt+X)"), [this] { emit clearMarksRequested(); });
    bar->addStretch(1);
    overlayBox_ = new QComboBox(this);
    overlayBox_->addItems({tr("No overlay"), tr("Clipping"), tr("False color")});
    overlayBox_->setAccessibleName(tr("Viewer overlay"));
    overlayBox_->setToolTip(tr("Exposure aids drawn over the picture (never exported)"));
    overlayBox_->setFocusPolicy(Qt::TabFocus);
    connect(overlayBox_, &QComboBox::currentIndexChanged, this,
            [this](int index) { view_->setOverlay(static_cast<render::ViewerOverlay>(std::max(0, index))); });
    bar->addWidget(overlayBox_);
    bar->addSpacing(8);
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
    for (QWidget* child : findChildren<QWidget*>()) child->installEventFilter(this);
    setActive(false);
    updateLabel();
}

bool ViewerPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) emit activated();
    return QWidget::eventFilter(watched, event);
}

void ViewerPanel::setTitle(const QString& title) { title_->setText(title); }

void ViewerPanel::setActive(bool active) {
    active_ = active;
    const auto& t = currentTokens();
    title_->setStyleSheet(QString("QLabel { color: %1; font-weight: %2; border-bottom: 2px solid %3; }")
                              .arg((active ? t.text : t.textMuted).name(), active ? "bold" : "normal",
                                   (active ? t.accent : t.border).name()));
}

void ViewerPanel::setMarks(std::optional<FrameIndex> in, std::optional<FrameIndex> out) { scrub_->setMarks(in, out); }

const Timeline* ViewerPanel::timeline() const {
    return project_ ? project_->findTimeline(timelineId_) : nullptr;
}

FrameIndex ViewerPanel::duration() const {
    const Timeline* tl = timeline();
    return tl ? tl->duration() : 0;
}

ViewerPanel::~ViewerPanel() { engine_->stop(); }

void ViewerPanel::setAudioOutput(std::shared_ptr<playback::AudioOutput> output) {
    stop();
    engine_ = std::make_unique<playback::PlaybackEngine>(std::move(output));
}

int ViewerPanel::droppedFrames() const { return static_cast<int>(engine_->stats().droppedFrames); }
bool ViewerPanel::playingWithAudio() const { return engine_->isRunning() && engine_->usingAudioClock(); }

void ViewerPanel::setSource(const Project* project, std::string timelineId) {
    stop();
    project_ = project;
    timelineId_ = std::move(timelineId);
    compositor_.reset();
    if (project_) compositor_ = std::make_unique<render::FrameCompositor>(render::resolverFor(*project_), 8);
    position_ = 0;
    refresh();
}

bool ViewerPanel::isPlaying() const { return engine_->isRunning(); }

std::optional<render::MixMeters> ViewerPanel::meters() const { return engine_->meters(); }
const QImage& ViewerPanel::currentImage() const { return view_->image(); }
const QImage& ViewerPanel::displayedImage() const { return view_->displayedImage(); }
void ViewerPanel::setOverlay(render::ViewerOverlay overlay) { overlayBox_->setCurrentIndex(static_cast<int>(overlay)); }
render::ViewerOverlay ViewerPanel::overlay() const { return static_cast<render::ViewerOverlay>(std::max(0, overlayBox_->currentIndex())); }

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
    if (!timeline()) return;
    if (isPlaying()) {
        stop();
        return;
    }
    const Timeline& tl = *timeline();
    if (tl.duration() == 0) return;
    if (position_ >= tl.duration() - 1) position_ = 0;
    const QSize size = previewSize();
    Status s = engine_->start(*project_, tl.id, position_, size.width(), size.height());
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
    setPosition(std::max<FrameIndex>(0, duration() - 1));
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
    if (!timeline() || !isPlaying()) return;
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
    const Timeline& tl = *timeline();
    // Preview at the display size (bounded by the timeline resolution) to keep playback fast.
    const QSize fit = QSize(tl.width, tl.height).scaled(view_->size().boundedTo(QSize(tl.width, tl.height)), Qt::KeepAspectRatio);
    return {std::max(2, fit.width() & ~1), std::max(2, fit.height() & ~1)};
}

void ViewerPanel::renderCurrent() {
    if (!timeline() || !compositor_) {
        view_->setImage({});
        return;
    }
    const QSize size = previewSize();
    auto frame = compositor_->render(*timeline(), position_, size.width(), size.height());
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
    const Timeline* tl = timeline();
    scrub_->setRange(tl ? tl->duration() : 0);
    scrub_->setPosition(position_);
    if (!tl) {
        timecode_->setText("--:--:--:--");
        return;
    }
    timecode_->setText(QString::fromStdString(formatTimecode(position_, tl->frameRate)) + " / " +
                       QString::fromStdString(formatTimecode(tl->duration(), tl->frameRate)));
}

ScrubBar::ScrubBar(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(fontMetrics().height());
    setFocusPolicy(Qt::NoFocus);
}

QSize ScrubBar::sizeHint() const { return {200, fontMetrics().height()}; }

void ScrubBar::setRange(FrameIndex duration) {
    duration_ = duration;
    update();
}

void ScrubBar::setPosition(FrameIndex frame) {
    position_ = frame;
    update();
}

void ScrubBar::setMarks(std::optional<FrameIndex> in, std::optional<FrameIndex> out) {
    markIn_ = in;
    markOut_ = out;
    update();
}

int ScrubBar::xAt(FrameIndex frame) const {
    if (duration_ <= 0) return 0;
    return static_cast<int>(static_cast<double>(frame) * (width() - 1) / static_cast<double>(duration_));
}

FrameIndex ScrubBar::frameAt(int x) const {
    if (duration_ <= 0 || width() <= 1) return 0;
    const auto f = static_cast<FrameIndex>(static_cast<double>(x) * static_cast<double>(duration_) / (width() - 1));
    return std::clamp<FrameIndex>(f, 0, std::max<FrameIndex>(0, duration_ - 1));
}

void ScrubBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& t = currentTokens();
    const int mid = height() / 2;
    p.fillRect(QRect(0, mid - 2, width(), 4), t.border);
    if (markIn_ || markOut_) {
        const int x0 = xAt(markIn_.value_or(0));
        const int x1 = xAt(markOut_.value_or(duration_));
        QColor range = t.accent;
        range.setAlpha(140);
        p.fillRect(QRect(x0, 1, std::max(2, x1 - x0), height() - 2), range);
        p.setPen(t.accent);
        if (markIn_) p.drawLine(x0, 0, x0, height());
        if (markOut_) p.drawLine(x1, 0, x1, height());
    }
    const int px = xAt(position_);
    p.setPen(QPen(t.playhead, 2));
    p.drawLine(px, 0, px, height());
}

void ScrubBar::mousePressEvent(QMouseEvent* event) { emit seekRequested(frameAt(event->pos().x())); }

void ScrubBar::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) emit seekRequested(frameAt(event->pos().x()));
}

}  // namespace up::ui

