#include "ui/InspectorPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "app/EditorSession.h"

namespace up::ui {

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    title_ = new QLabel(this);
    title_->setWordWrap(true);
    layout->addWidget(title_);
    form_ = new QWidget(this);
    auto* grid = new QGridLayout(form_);
    grid->setContentsMargins(0, 0, 0, 0);
    int r = 0;
    for (ClipParam p : kAllClipParams) {
        const ClipParamInfo& info = paramInfo(p);
        Row& row = rows_[static_cast<std::size_t>(p)];
        auto* label = new QLabel(tr(info.label), form_);
        row.value = new QDoubleSpinBox(form_);
        row.value->setRange(info.minimum, info.maximum);
        row.value->setDecimals(1);
        row.value->setSuffix(QString(" ") + QString::fromUtf8(info.unit));
        row.value->setKeyboardTracking(false);  // commit on Enter / focus out / arrows, not every digit
        row.value->setAccessibleName(tr(info.label));
        label->setBuddy(row.value);
        auto makeButton = [&](const QString& text, const QString& tip) {
            auto* b = new QToolButton(form_);
            b->setText(text);
            b->setToolTip(tip);
            b->setAccessibleName(tip + " " + tr(info.label));
            return b;
        };
        row.previous = makeButton("◀", tr("Previous keyframe"));
        row.key = makeButton("◆", tr("Keyframe at playhead"));
        row.key->setCheckable(true);
        row.next = makeButton("▶", tr("Next keyframe"));
        row.interpolation = new QComboBox(form_);
        row.interpolation->addItems({tr("Linear"), tr("Hold"), tr("Ease")});
        row.interpolation->setToolTip(tr("Interpolation from the keyframe at the playhead to the next one"));
        row.reset = makeButton("↺", tr("Reset"));
        grid->addWidget(label, r, 0);
        grid->addWidget(row.value, r, 1);
        grid->addWidget(row.previous, r, 2);
        grid->addWidget(row.key, r, 3);
        grid->addWidget(row.next, r, 4);
        grid->addWidget(row.interpolation, r, 5);
        grid->addWidget(row.reset, r, 6);
        ++r;

        connect(row.value, &QDoubleSpinBox::valueChanged, this, [this, p] {
            if (!updating_) commitValue(p);
        });
        connect(row.key, &QToolButton::clicked, this, [this, p] { toggleKey(p); });
        connect(row.previous, &QToolButton::clicked, this, [this, p] { jump(p, false); });
        connect(row.next, &QToolButton::clicked, this, [this, p] { jump(p, true); });
        connect(row.interpolation, &QComboBox::currentIndexChanged, this, [this, p](int index) {
            if (updating_ || !session_) return;
            report(session_->setKeyframeInterpolation(clipId_, p, playhead_, static_cast<Interpolation>(index)));
        });
        connect(row.reset, &QToolButton::clicked, this, [this, p] {
            if (session_) report(session_->resetClipParameter(clipId_, p));
        });
    }
    layout->addWidget(form_);
    layout->addStretch(1);
    refresh();
}

void InspectorPanel::setSession(EditorSession* session) {
    session_ = session;
    clipId_.clear();
    refresh();
}

void InspectorPanel::setClip(const std::string& clipId) {
    clipId_.clear();
    if (session_ && !clipId.empty()) {
        const Timeline& tl = session_->timeline();
        const Track* track = tl.trackOfClip(clipId);
        if (track && track->kind == TrackKind::Video) {
            clipId_ = clipId;
        } else if (track) {
            for (const auto& partner : tl.linkedClips(clipId))
                if (tl.trackOfClip(partner)->kind == TrackKind::Video) clipId_ = partner;
        }
    }
    refresh();
}

void InspectorPanel::setPlayhead(FrameIndex frame) {
    playhead_ = frame;
    refresh();
}

void InspectorPanel::refresh() {
    const Clip* clip = session_ && !clipId_.empty() ? session_->timeline().clip(clipId_) : nullptr;
    form_->setEnabled(clip != nullptr);
    if (!clip) {
        clipId_.clear();
        title_->setText(tr("Select a video clip to edit its transform."));
        return;
    }
    const bool inside = clip->contains(playhead_);
    title_->setText(inside ? QString::fromStdString(clip->name)
                           : tr("%1 — move the playhead onto the clip to set keyframes").arg(QString::fromStdString(clip->name)));
    // Outside the clip, show the value at the nearest clip frame.
    const FrameIndex at = std::clamp(playhead_, clip->start, clip->end() - 1);
    const FrameIndex source = clip->toSource(at);
    updating_ = true;
    for (ClipParam p : kAllClipParams) {
        const Row& row = rows_[static_cast<std::size_t>(p)];
        const AnimatedValue& v = clip->transform[p];
        row.value->setValue(v.at(source));
        const Keyframe* key = inside ? v.keyAt(source) : nullptr;
        row.key->setChecked(key != nullptr);
        row.key->setEnabled(inside);
        row.interpolation->setEnabled(key != nullptr);
        if (key) row.interpolation->setCurrentIndex(static_cast<int>(key->interpolation));
        const bool hasEarlier = std::any_of(v.keys.begin(), v.keys.end(), [&](const Keyframe& k) { return k.frame < source; });
        const bool hasLater = std::any_of(v.keys.begin(), v.keys.end(), [&](const Keyframe& k) { return k.frame > source; });
        row.previous->setEnabled(hasEarlier);
        row.next->setEnabled(hasLater);
        row.reset->setEnabled(v.animated() || v.value != paramInfo(p).defaultValue);
        // Animated parameters are highlighted so it is clear an edit sets a keyframe.
        QFont f = row.value->font();
        f.setBold(v.animated());
        row.value->setFont(f);
    }
    updating_ = false;
}

void InspectorPanel::commitValue(ClipParam param) {
    if (!session_ || clipId_.empty()) return;
    report(session_->setClipParameter(clipId_, param, rows_[static_cast<std::size_t>(param)].value->value(), playhead_));
}

void InspectorPanel::toggleKey(ClipParam param) {
    if (!session_ || clipId_.empty()) return;
    const bool add = rows_[static_cast<std::size_t>(param)].key->isChecked();
    report(session_->setKeyframe(clipId_, param, playhead_, add));
}

void InspectorPanel::jump(ClipParam param, bool forward) {
    const Clip* clip = session_ ? session_->timeline().clip(clipId_) : nullptr;
    if (!clip) return;
    const FrameIndex source = clip->toSource(std::clamp(playhead_, clip->start, clip->end() - 1));
    const auto& keys = clip->transform[param].keys;
    std::optional<FrameIndex> target;
    for (const auto& k : keys) {
        if (forward && k.frame > source && (!target || k.frame < *target)) target = k.frame;
        if (!forward && k.frame < source && (!target || k.frame > *target)) target = k.frame;
    }
    if (target) emit seekRequested(clip->toTimeline(*target));
}

void InspectorPanel::report(const Status& status) {
    if (!status.ok()) {
        emit errorOccurred(QString::fromStdString(status.error().message));
        refresh();  // put the widgets back to what the model says
    }
}

}  // namespace up::ui
