#include "ui/TimelineView.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "app/EditorSession.h"
#include "app/MediaAssets.h"
#include "core/Timecode.h"
#include "ui/FrameImage.h"
#include "ui/MediaPoolPanel.h"
#include "ui/Theme.h"

namespace up::ui {
namespace {

constexpr double kMinPixelsPerFrame = 0.05;
constexpr double kMaxPixelsPerFrame = 40.0;
constexpr double kZoomStep = 1.25;

QString errorText(const Error& e) { return QString::fromStdString(e.toString()); }

}  // namespace

TimelineView::TimelineView(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Timeline"));
    setMinimumHeight(metricsFor(this).rulerHeight + metricsFor(this).trackHeight * 2);
}

void TimelineView::setSession(EditorSession* session) {
    session_ = session;
    selected_.clear();
    playhead_ = 0;
    scroll_ = 0;
    const auto m = metricsFor(this);
    const int rows = session_ ? static_cast<int>(session_->timeline().tracks.size()) : 2;
    setMinimumHeight(m.rulerHeight + m.trackHeight * rows);
    update();
    emit viewChanged();
}

std::vector<std::string> TimelineView::rowTrackIds() const {
    std::vector<std::string> rows;
    if (!session_) return rows;
    const Timeline& tl = session_->timeline();
    auto video = tl.trackIdsOfKind(TrackKind::Video);
    std::reverse(video.begin(), video.end());  // highest video track at the top
    rows = video;
    for (const auto& id : tl.trackIdsOfKind(TrackKind::Audio)) rows.push_back(id);
    return rows;
}

int TimelineView::rowTop(int row) const {
    const auto m = metricsFor(this);
    return m.rulerHeight + row * m.trackHeight;
}

int TimelineView::rowAt(int y) const {
    const auto m = metricsFor(this);
    if (y < m.rulerHeight) return -1;
    const int row = (y - m.rulerHeight) / m.trackHeight;
    return row < static_cast<int>(rowTrackIds().size()) ? row : -1;
}

int TimelineView::xForFrame(FrameIndex frame) const {
    return metricsFor(this).trackHeaderWidth + static_cast<int>(std::lround(static_cast<double>(frame - scroll_) * pixelsPerFrame_));
}

FrameIndex TimelineView::frameForX(int x) const {
    const int local = x - metricsFor(this).trackHeaderWidth;
    return std::max<FrameIndex>(0, scroll_ + static_cast<FrameIndex>(std::floor(local / pixelsPerFrame_ + 0.5)));
}

FrameIndex TimelineView::contentFrames() const {
    return session_ ? session_->timeline().duration() : 0;
}

FrameIndex TimelineView::visibleFrames() const {
    const int w = width() - metricsFor(this).trackHeaderWidth;
    return std::max<FrameIndex>(1, static_cast<FrameIndex>(w / pixelsPerFrame_));
}

void TimelineView::setPixelsPerFrame(double ppf) {
    pixelsPerFrame_ = std::clamp(ppf, kMinPixelsPerFrame, kMaxPixelsPerFrame);
    update();
    emit viewChanged();
}

void TimelineView::setScrollFrame(FrameIndex frame) {
    scroll_ = std::max<FrameIndex>(0, frame);
    update();
    emit viewChanged();
}

void TimelineView::zoomIn() { setPixelsPerFrame(pixelsPerFrame_ * kZoomStep); }
void TimelineView::zoomOut() { setPixelsPerFrame(pixelsPerFrame_ / kZoomStep); }

void TimelineView::zoomToFit() {
    const FrameIndex frames = std::max<FrameIndex>(contentFrames(), 25);
    const int w = width() - metricsFor(this).trackHeaderWidth - 20;
    scroll_ = 0;
    setPixelsPerFrame(std::max(1, w) / static_cast<double>(frames));
}

void TimelineView::setPlayhead(FrameIndex frame) {
    frame = std::max<FrameIndex>(0, frame);
    if (frame == playhead_) return;
    playhead_ = frame;
    // Page-follow the playhead during playback / stepping.
    if (frame < scroll_ || frame >= scroll_ + visibleFrames()) setScrollFrame(std::max<FrameIndex>(0, frame - visibleFrames() / 10));
    update();
}

void TimelineView::selectClip(const QString& clipId) {
    selected_ = clipId.toStdString();
    update();
    emit selectionChanged(clipId);
}

QRect TimelineView::clipRect(const std::string& clipId) const {
    if (!session_) return {};
    const auto rows = rowTrackIds();
    const Timeline& tl = session_->timeline();
    const Track* track = tl.trackOfClip(clipId);
    const Clip* clip = tl.clip(clipId);
    if (!track || !clip) return {};
    const int row = static_cast<int>(std::find(rows.begin(), rows.end(), track->id) - rows.begin());
    const auto m = metricsFor(this);
    const int x0 = xForFrame(clip->start);
    const int x1 = xForFrame(clip->end());
    return QRect(x0, rowTop(row) + 2, std::max(2, x1 - x0), m.trackHeight - 4);
}

std::optional<TimelineView::Hit> TimelineView::hitTest(const QPoint& pos) const {
    if (!session_) return std::nullopt;
    const int row = rowAt(pos.y());
    if (row < 0 || pos.x() < metricsFor(this).trackHeaderWidth) return std::nullopt;
    const Track* track = session_->timeline().track(rowTrackIds()[static_cast<std::size_t>(row)]);
    const int handle = metricsFor(this).trimHandle;
    for (const auto& c : track->clips) {
        const QRect r = clipRect(c.id);
        if (pos.x() < r.left() - 1 || pos.x() > r.right() + 1) continue;
        Hit hit{c.id, Zone::Body};
        if (r.width() > handle * 3) {
            if (pos.x() <= r.left() + handle) hit.zone = Zone::In;
            else if (pos.x() >= r.right() - handle) hit.zone = Zone::Out;
        }
        return hit;
    }
    return std::nullopt;
}

FrameIndex TimelineView::snap(FrameIndex frame, const std::vector<std::string>& exclude) const {
    if (!session_) return frame;
    const double maxDistance = metricsFor(this).snapDistance / pixelsPerFrame_;
    FrameIndex best = frame;
    double bestDistance = maxDistance + 1;
    auto consider = [&](FrameIndex candidate) {
        const double d = std::abs(static_cast<double>(candidate - frame));
        if (d <= maxDistance && d < bestDistance) {
            best = candidate;
            bestDistance = d;
        }
    };
    consider(playhead_);
    consider(0);
    for (const auto& t : session_->timeline().tracks) {
        for (const auto& c : t.clips) {
            if (std::find(exclude.begin(), exclude.end(), c.id) != exclude.end()) continue;
            consider(c.start);
            consider(c.end());
        }
    }
    return best;
}

QRect TimelineView::toggleRect(int row, int index) const {
    const auto m = metricsFor(this);
    const int size = m.trackHeight / 3;
    const int x = m.trackHeaderWidth - (index + 1) * (size + 4) - 4;
    return QRect(x, rowTop(row) + m.trackHeight - size - 6, size, size);
}

QRect TimelineView::targetRect(int row) const {
    const auto m = metricsFor(this);
    const int size = m.trackHeight / 3;
    return QRect(4, rowTop(row) + m.trackHeight - size - 6, size + 4, size);
}

bool TimelineView::handleHeaderClick(const QPoint& pos) {
    const int row = rowAt(pos.y());
    if (row < 0 || !session_) return false;
    const std::string trackId = rowTrackIds()[static_cast<std::size_t>(row)];
    const Track* track = session_->timeline().track(trackId);
    if (targetRect(row).contains(pos)) {
        // Toggle source patching: target this track, or disable the stream if it already is.
        const Timeline& tl = session_->timeline();
        std::string video = tl.videoTarget;
        std::string audio = tl.audioTarget;
        std::string& target = track->kind == TrackKind::Video ? video : audio;
        target = target == trackId ? std::string() : trackId;
        Status st = session_->setTrackTargets(video, audio);
        if (!st.ok()) report(errorText(st.error()));
        return true;
    }
    TrackState s{track->enabled, track->locked, track->muted, track->solo, track->gainDb};
    // Toggle order from the right edge: lock, enable/mute, solo (audio only).
    if (toggleRect(row, 0).contains(pos)) s.locked = !s.locked;
    else if (toggleRect(row, 1).contains(pos)) {
        if (track->kind == TrackKind::Video) s.enabled = !s.enabled;
        else s.muted = !s.muted;
    } else if (track->kind == TrackKind::Audio && toggleRect(row, 2).contains(pos)) s.solo = !s.solo;
    else return true;  // header click without a toggle: consume
    Status st = session_->setTrackState(trackId, s);
    if (!st.ok()) report(errorText(st.error()));
    return true;
}

void TimelineView::report(const QString& message) { emit errorOccurred(message); }

void TimelineView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (!session_ || event->button() != Qt::LeftButton) return;
    const auto m = metricsFor(this);
    pressPos_ = event->pos();
    if (event->pos().y() < m.rulerHeight) {
        drag_ = DragKind::Scrub;
        const FrameIndex f = frameForX(event->pos().x());
        setPlayhead(f);
        emit playheadMoved(f);
        return;
    }
    if (event->pos().x() < m.trackHeaderWidth) {
        handleHeaderClick(event->pos());
        return;
    }
    const auto hit = hitTest(event->pos());
    if (!hit) {
        selectClip({});
        return;
    }
    selectClip(QString::fromStdString(hit->clipId));
    dragClip_ = hit->clipId;
    pressFrame_ = frameForX(event->pos().x());
    dragDelta_ = 0;
    dragRow_ = rowAt(event->pos().y());
    dragRipple_ = event->modifiers() & Qt::ShiftModifier;
    drag_ = hit->zone == Zone::In ? DragKind::TrimIn : hit->zone == Zone::Out ? DragKind::TrimOut : DragKind::Move;
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
    if (!session_) return;
    if (drag_ == DragKind::None) {
        const auto hit = hitTest(event->pos());
        setCursor(hit && hit->zone != Zone::Body ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (drag_ == DragKind::Scrub) {
        const FrameIndex f = frameForX(event->pos().x());
        setPlayhead(f);
        emit playheadMoved(f);
        return;
    }
    const Clip* clip = session_->timeline().clip(dragClip_);
    if (!clip) return;
    std::vector<std::string> exclude = session_->timeline().linkedClips(dragClip_);
    exclude.push_back(dragClip_);
    const FrameIndex raw = frameForX(event->pos().x()) - pressFrame_;
    if (drag_ == DragKind::Move) {
        // Snap whichever edge of the moved clip lands closer to an edit point.
        const FrameIndex startSnap = snap(clip->start + raw, exclude) - clip->start;
        const FrameIndex endSnap = snap(clip->end() + raw, exclude) - clip->end();
        dragDelta_ = std::abs(startSnap - raw) <= std::abs(endSnap - raw) ? startSnap : endSnap;
        dragDelta_ = std::max(dragDelta_, -clip->start);
        const int row = rowAt(event->pos().y());
        if (row >= 0) dragRow_ = row;
    } else {
        const FrameIndex edge = drag_ == DragKind::TrimIn ? clip->start : clip->end();
        dragDelta_ = snap(edge + raw, exclude) - edge;
    }
    emit statusMessage(tr("%1 %2 frames").arg(drag_ == DragKind::Move ? tr("Move") : tr("Trim")).arg(dragDelta_));
    update();
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const DragKind kind = drag_;
    drag_ = DragKind::None;
    if (!session_ || kind == DragKind::None || kind == DragKind::Scrub) {
        update();
        return;
    }
    const Clip* clip = session_->timeline().clip(dragClip_);
    const auto rows = rowTrackIds();
    Status st = Status::success();
    if (clip && kind == DragKind::Move) {
        const std::string target = dragRow_ >= 0 ? rows[static_cast<std::size_t>(dragRow_)] : session_->timeline().trackOfClip(dragClip_)->id;
        const Track* targetTrack = session_->timeline().track(target);
        const Track* sourceTrack = session_->timeline().trackOfClip(dragClip_);
        const bool sameKind = targetTrack && sourceTrack && targetTrack->kind == sourceTrack->kind;
        if (dragDelta_ != 0 || (sameKind && target != sourceTrack->id))
            st = session_->moveClip(dragClip_, sameKind ? target : sourceTrack->id, clip->start + dragDelta_);
    } else if (clip && dragDelta_ != 0) {
        st = session_->trimClip(dragClip_, kind == DragKind::TrimIn ? ops::Edge::In : ops::Edge::Out, dragDelta_,
                                dragRipple_ ? ops::TrimMode::Ripple : ops::TrimMode::Normal);
    }
    if (!st.ok()) report(errorText(st.error()));
    dragDelta_ = 0;
    update();
}

void TimelineView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom around the cursor.
        const FrameIndex anchor = frameForX(static_cast<int>(event->position().x()));
        const double before = static_cast<double>(anchor - scroll_);
        const double oldPpf = pixelsPerFrame_;
        setPixelsPerFrame(delta > 0 ? pixelsPerFrame_ * kZoomStep : pixelsPerFrame_ / kZoomStep);
        setScrollFrame(anchor - static_cast<FrameIndex>(before * oldPpf / pixelsPerFrame_));
    } else {
        setScrollFrame(scroll_ - static_cast<FrameIndex>(delta / 120.0 * static_cast<double>(visibleFrames()) / 10.0));
    }
    event->accept();
}

void TimelineView::dragEnterEvent(QDragEnterEvent* event) {
    if (session_ && event->mimeData()->hasFormat(kMediaMimeType)) event->acceptProposedAction();
}

void TimelineView::dragMoveEvent(QDragMoveEvent* event) {
    if (!session_ || !event->mimeData()->hasFormat(kMediaMimeType)) return;
    dropFrame_ = snap(frameForX(event->position().toPoint().x()), {});
    dropRow_ = rowAt(event->position().toPoint().y());
    event->acceptProposedAction();
    update();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent*) {
    dropFrame_.reset();
    update();
}

void TimelineView::dropEvent(QDropEvent* event) {
    if (!session_) return;
    const std::string mediaId = QString::fromUtf8(event->mimeData()->data(kMediaMimeType)).toStdString();
    const FrameIndex at = snap(frameForX(event->position().toPoint().x()), {});
    const int row = rowAt(event->position().toPoint().y());
    std::string videoTrack, audioTrack;
    if (row >= 0) {
        const std::string id = rowTrackIds()[static_cast<std::size_t>(row)];
        (session_->timeline().track(id)->kind == TrackKind::Video ? videoTrack : audioTrack) = id;
    }
    const auto mode = (event->modifiers() & Qt::ControlModifier) ? ops::EditMode::Insert : ops::EditMode::Overwrite;
    auto placed = session_->placeMedia(mediaId, at, mode, videoTrack, audioTrack);
    dropFrame_.reset();
    if (!placed.ok()) {
        report(errorText(placed.error()));
    } else {
        if (!placed.value().empty()) selectClip(QString::fromStdString(placed.value().front()));
        event->acceptProposedAction();
    }
    update();
}

void TimelineView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    emit viewChanged();
}

void TimelineView::drawTrackHeader(QPainter& p, const Track& track, int row) const {
    const auto& t = currentTokens();
    const auto m = metricsFor(this);
    const QRect header(0, rowTop(row), m.trackHeaderWidth, m.trackHeight);
    p.fillRect(header, track.locked ? t.trackLocked : t.panelAlt);
    p.setPen(t.border);
    p.drawLine(header.bottomLeft(), header.bottomRight());
    p.setPen(t.text);
    p.drawText(header.adjusted(8, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop, QString::fromStdString(track.name));
    struct Toggle {
        QString label;
        bool on;
    };
    std::vector<Toggle> toggles{{"L", track.locked}};
    if (track.kind == TrackKind::Video) toggles.push_back({"V", track.enabled});
    else {
        toggles.push_back({"M", track.muted});
        toggles.push_back({"S", track.solo});
    }
    // Source patch box.
    const Timeline& tl = session_->timeline();
    const bool targeted = track.id == (track.kind == TrackKind::Video ? tl.videoTarget : tl.audioTarget);
    const QRect tr = targetRect(row);
    p.fillRect(tr, targeted ? t.accent : t.panel);
    p.setPen(targeted ? t.accentText : t.textMuted);
    p.drawRect(tr.adjusted(0, 0, -1, -1));
    p.drawText(tr, Qt::AlignCenter, track.kind == TrackKind::Video ? "V" : "A");
    for (std::size_t i = 0; i < toggles.size(); ++i) {
        const QRect r = toggleRect(row, static_cast<int>(i));
        p.fillRect(r, toggles[i].on ? t.toggleOn : t.panel);
        p.setPen(toggles[i].on ? t.accentText : t.textMuted);
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.drawText(r, Qt::AlignCenter, toggles[i].label);
    }
}

void TimelineView::drawClip(QPainter& p, const Clip& clip, const QRect& r, bool isVideo) const {
    const auto& t = currentTokens();
    const MediaItem* media = session_->project().findMedia(clip.mediaId);
    QColor fill = !media || !media->online ? t.offline : (isVideo ? t.videoClip : t.audioClip);
    if (!clip.enabled) fill = fill.darker(180);
    p.fillRect(r, fill);
    int labelLeft = r.left() + 5;
    if (assets_ && media && media->online) {
        if (isVideo) {
            // Poster frame at the head of the clip, when there is room for it.
            if (auto thumb = assets_->thumbnail(*media)) {
                const QRect area = r.adjusted(1, 1, -1, -1);
                const QSize size = QSize(thumb->width, thumb->height).scaled(QSize(area.width() / 2, area.height()), Qt::KeepAspectRatio);
                if (size.width() >= 16) {
                    p.drawImage(QRect(area.topLeft(), size), toQImage(*thumb));
                    labelLeft = area.left() + size.width() + 4;
                }
            }
        } else if (auto peaks = assets_->waveform(*media)) {
            drawWaveform(p, clip, r, *peaks);
        }
    }
    const bool selected = clip.id == selected_ ||
                          (!selected_.empty() && !clip.linkId.empty() && session_->timeline().clip(selected_) &&
                           session_->timeline().clip(selected_)->linkId == clip.linkId);
    p.setPen(QPen(selected ? t.selection : fill.darker(150), selected ? 2 : 1));
    p.drawRect(r.adjusted(0, 0, -1, -1));
    if (r.right() - labelLeft > 16) {
        p.setPen(t.clipText);
        QString label = QString::fromStdString(clip.name);
        if (!media || !media->online) label = tr("OFFLINE — %1").arg(label);
        const QRect textRect(labelLeft, r.top() + 3, r.right() - labelLeft - 3, r.height() - 6);
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, p.fontMetrics().elidedText(label, Qt::ElideRight, textRect.width()));
    }
}

void TimelineView::drawWaveform(QPainter& p, const Clip& clip, const QRect& r, const media::WaveformPeaks& peaks) const {
    const auto& t = currentTokens();
    const double fps = session_->timeline().frameRate.toDouble();
    const QRect area = r.adjusted(1, p.fontMetrics().height() + 2, -1, -2);
    if (area.height() < 4) return;
    const int mid = area.center().y();
    const double half = area.height() / 2.0;
    QColor color = t.clipText;
    color.setAlpha(150);
    p.setPen(color);
    // One vertical line per pixel column, covering the source time under that column.
    const int x0 = std::max(area.left(), metricsFor(this).trackHeaderWidth);
    const int x1 = std::min(area.right(), width());
    for (int x = x0; x <= x1; ++x) {
        const double srcFrame = static_cast<double>(clip.sourceIn) + (x - r.left()) / pixelsPerFrame_;
        const auto [lo, hi] = peaks.range(srcFrame / fps, (srcFrame + 1.0 / pixelsPerFrame_) / fps);
        const int top = mid - static_cast<int>(std::lround(hi * half));
        const int bottom = mid - static_cast<int>(std::lround(lo * half));
        p.drawLine(x, top, x, std::max(top, bottom));
    }
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& t = currentTokens();
    const auto m = metricsFor(this);
    p.fillRect(rect(), t.panel);
    if (!session_) {
        p.setPen(t.textMuted);
        p.drawText(rect(), Qt::AlignCenter, tr("No project open"));
        return;
    }
    const Timeline& tl = session_->timeline();
    const auto rows = rowTrackIds();

    // Ruler with adaptive tick spacing (at least ~80 px between labels).
    p.fillRect(QRect(0, 0, width(), m.rulerHeight), t.panelAlt);
    // Tick steps in whole frames: 1, 5, 10 frames, then whole seconds/minutes.
    const auto fps = static_cast<FrameIndex>(std::max(1, nominalFps(tl.frameRate)));
    const FrameIndex steps[] = {1, 5, 10, fps, fps * 2, fps * 5, fps * 10, fps * 30, fps * 60, fps * 120, fps * 300, fps * 600};
    FrameIndex stepFrames = fps * 600;
    for (FrameIndex s : steps) {
        if (static_cast<double>(s) * pixelsPerFrame_ >= 80) {
            stepFrames = s;
            break;
        }
    }
    p.setPen(t.textMuted);
    const FrameIndex first = scroll_ - (scroll_ % stepFrames);
    for (FrameIndex f = first; xForFrame(f) < width(); f += stepFrames) {
        const int x = xForFrame(f);
        if (x < m.trackHeaderWidth) continue;
        p.drawLine(x, m.rulerHeight - 6, x, m.rulerHeight);
        p.drawText(x + 3, m.rulerHeight - 8, QString::fromStdString(formatTimecode(f, tl.frameRate)));
    }

    // Tracks and clips.
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const Track& track = *tl.track(rows[row]);
        const int y = rowTop(static_cast<int>(row));
        p.fillRect(QRect(m.trackHeaderWidth, y, width(), m.trackHeight), row % 2 ? t.panel : t.panelAlt.darker(105));
        p.setPen(t.border);
        p.drawLine(m.trackHeaderWidth, y + m.trackHeight - 1, width(), y + m.trackHeight - 1);
        p.setClipRect(QRect(m.trackHeaderWidth, y, width() - m.trackHeaderWidth, m.trackHeight));
        for (const auto& clip : track.clips) {
            const QRect r = clipRect(clip.id);
            if (r.right() < m.trackHeaderWidth || r.left() > width()) continue;
            drawClip(p, clip, r, track.kind == TrackKind::Video);
        }
        p.setClipping(false);
        drawTrackHeader(p, track, static_cast<int>(row));
    }

    // Timeline in/out range (three-point editing).
    if (tl.markIn || tl.markOut) {
        const int x0 = std::max(m.trackHeaderWidth, xForFrame(tl.markIn.value_or(0)));
        const int x1 = tl.markOut ? xForFrame(*tl.markOut) : width();
        if (x1 > x0) {
            QColor shade = t.accent;
            shade.setAlpha(45);
            p.fillRect(QRect(x0, 0, x1 - x0, height()), shade);
            shade.setAlpha(200);
            p.fillRect(QRect(x0, m.rulerHeight - 4, x1 - x0, 4), shade);
            p.setPen(t.accent);
            if (tl.markIn && x0 > m.trackHeaderWidth) p.drawLine(x0, 0, x0, height());
            if (tl.markOut) p.drawLine(x1, 0, x1, height());
        }
    }

    // Drag previews.
    if (drag_ == DragKind::Move || drag_ == DragKind::TrimIn || drag_ == DragKind::TrimOut) {
        if (tl.clip(dragClip_)) {
            QRect r = clipRect(dragClip_);
            const int dx = static_cast<int>(std::lround(static_cast<double>(dragDelta_) * pixelsPerFrame_));
            if (drag_ == DragKind::Move) {
                r.translate(dx, dragRow_ >= 0 ? rowTop(dragRow_) + 2 - r.top() : 0);
            } else if (drag_ == DragKind::TrimIn) {
                r.setLeft(r.left() + dx);
            } else {
                r.setRight(r.right() + dx);
            }
            p.setPen(QPen(t.selection, 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.normalized());
        }
    }
    if (dropFrame_) {
        const int x = xForFrame(*dropFrame_);
        p.setPen(QPen(t.accent, 2));
        p.drawLine(x, m.rulerHeight, x, height());
    }

    // Playhead.
    const int px = xForFrame(playhead_);
    if (px >= m.trackHeaderWidth) {
        p.setPen(QPen(t.playhead, 2));
        p.drawLine(px, 0, px, height());
    }
}

TimelinePanel::TimelinePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    view_ = new TimelineView(this);
    scrollBar_ = new QScrollBar(Qt::Horizontal, this);
    scrollBar_->setAccessibleName(tr("Timeline scroll"));
    layout->addWidget(view_, 1);
    layout->addWidget(scrollBar_);
    connect(view_, &TimelineView::viewChanged, this, &TimelinePanel::syncScrollBar);
    connect(scrollBar_, &QScrollBar::valueChanged, this, [this](int v) {
        if (v != static_cast<int>(view_->scrollFrame())) view_->setScrollFrame(v);
    });
}

void TimelinePanel::syncScrollBar() {
    const FrameIndex visible = view_->visibleFrames();
    const FrameIndex total = std::max(view_->contentFrames() + visible / 2, view_->scrollFrame() + visible);
    const QSignalBlocker block(scrollBar_);
    scrollBar_->setRange(0, static_cast<int>(std::max<FrameIndex>(0, total - visible)));
    scrollBar_->setPageStep(static_cast<int>(visible));
    scrollBar_->setSingleStep(std::max(1, static_cast<int>(visible / 20)));
    scrollBar_->setValue(static_cast<int>(view_->scrollFrame()));
}

}  // namespace up::ui
