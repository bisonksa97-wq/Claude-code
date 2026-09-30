#include "ui/MixerPanel.h"

#include <QDial>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "app/EditorSession.h"
#include "ui/EffectsDialog.h"
#include "ui/Theme.h"
#include "ui/ViewerPanel.h"

namespace up::ui {
namespace {

double toDb(float linear) {
    return linear <= 0.0f ? LevelMeter::kFloorDb : std::max(LevelMeter::kFloorDb, 20.0 * std::log10(static_cast<double>(linear)));
}

}  // namespace

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent) {
    clock_.start();
    setMinimumWidth(fontMetrics().height());
    setAccessibleName(tr("Level meter"));
}

QSize LevelMeter::sizeHint() const { return {fontMetrics().height(), fontMetrics().height() * 8}; }

void LevelMeter::setLevels(float peakLeft, float peakRight) {
    const qint64 now = clock_.elapsed();
    const double dt = static_cast<double>(now - last_) / 1000.0;
    last_ = now;
    const float peaks[2] = {peakLeft, peakRight};
    for (int c = 0; c < 2; ++c) {
        const double db = toDb(peaks[c]);
        // Instant attack, linear-in-dB fall-off.
        shown_[c] = std::max(db, std::max(kFloorDb, shown_[c] - kFallDbPerSecond * dt));
        if (db >= hold_[c] || now - holdSince_[c] > kHoldMs) {
            hold_[c] = db;
            holdSince_[c] = now;
        }
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const auto& t = currentTokens();
    p.fillRect(rect(), t.panelAlt.darker(130));
    const int barWidth = std::max(2, (width() - 3) / 2);
    auto yFor = [&](double db) { return static_cast<int>(height() * (db / kFloorDb)); };
    for (int c = 0; c < 2; ++c) {
        const QRect bar(1 + c * (barWidth + 1), yFor(shown_[c]), barWidth, height() - yFor(shown_[c]));
        // Green below -18 dBFS, amber to -6, red above: fixed meaning, independent of theme.
        QLinearGradient g(0, height(), 0, 0);
        g.setColorAt(0.0, QColor("#30a46c"));
        g.setColorAt(1.0 - 18.0 / -kFloorDb, QColor("#30a46c"));
        g.setColorAt(1.0 - 6.0 / -kFloorDb, QColor("#ffc53d"));
        g.setColorAt(1.0, QColor("#e5484d"));
        p.fillRect(bar, g);
        if (hold_[c] > kFloorDb) {
            p.setPen(t.text);
            const int y = yFor(hold_[c]);
            p.drawLine(bar.left(), y, bar.right(), y);
        }
    }
}

MixerPanel::MixerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    stripsHost_ = new QWidget(scroll);
    stripsLayout_ = new QHBoxLayout(stripsHost_);
    stripsLayout_->setContentsMargins(0, 0, 0, 0);
    stripsLayout_->addStretch(1);
    scroll->setWidget(stripsHost_);
    layout->addWidget(scroll, 1);

    auto* masterBox = new QVBoxLayout;
    auto* masterLabel = new QLabel(tr("Master"), this);
    masterLabel->setAlignment(Qt::AlignCenter);
    master_ = new LevelMeter(this);
    master_->setAccessibleName(tr("Master level"));
    masterBox->addWidget(masterLabel);
    master_->setMinimumWidth(fontMetrics().height() * 2);
    masterBox->addWidget(master_, 1);
    layout->addLayout(masterBox);

    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, &MixerPanel::tick);
    timer_->start();
}

void MixerPanel::setSession(EditorSession* session) {
    session_ = session;
    strips_.clear();
    rebuild();
}

void MixerPanel::rebuild() {
    // Remove old strips (everything but the trailing stretch).
    while (stripsLayout_->count() > 1) {
        QLayoutItem* item = stripsLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    strips_.clear();
    if (!session_) return;
    for (const Track* track : session_->timeline().tracksOfKind(TrackKind::Audio)) {
        const std::string id = track->id;
        auto* column = new QWidget(stripsHost_);
        auto* box = new QVBoxLayout(column);
        box->setContentsMargins(2, 0, 2, 0);
        Strip s;
        s.trackId = id;
        s.name = new QLabel(column);
        s.name->setAlignment(Qt::AlignCenter);
        s.effects = new QPushButton(column);
        s.effects->setToolTip(tr("Insert effects"));
        auto* levels = new QHBoxLayout;
        s.meter = new LevelMeter(column);
        s.fader = new QSlider(Qt::Vertical, column);
        s.fader->setRange(-600, 120);  // tenths of a dB: -60 .. +12 dB
        s.fader->setPageStep(30);
        s.fader->setAccessibleName(tr("Track gain"));
        levels->addWidget(s.meter);
        levels->addWidget(s.fader);
        s.gainLabel = new QLabel(column);
        s.gainLabel->setAlignment(Qt::AlignCenter);
        s.pan = new QDial(column);
        s.pan->setRange(-100, 100);
        s.pan->setNotchesVisible(true);
        s.pan->setFixedSize(fontMetrics().height() * 3, fontMetrics().height() * 3);
        s.pan->setAccessibleName(tr("Track pan"));
        auto* buttons = new QHBoxLayout;
        s.mute = new QToolButton(column);
        s.mute->setText("M");
        s.mute->setCheckable(true);
        s.mute->setToolTip(tr("Mute"));
        s.solo = new QToolButton(column);
        s.solo->setText("S");
        s.solo->setCheckable(true);
        s.solo->setToolTip(tr("Solo"));
        buttons->addWidget(s.mute);
        buttons->addWidget(s.solo);
        box->addWidget(s.name);
        box->addWidget(s.effects);
        box->addLayout(levels, 1);
        box->addWidget(s.gainLabel);
        box->addWidget(s.pan, 0, Qt::AlignHCenter);
        box->addLayout(buttons);
        stripsLayout_->insertWidget(stripsLayout_->count() - 1, column);

        // Commit when a drag ends (one undo step), or immediately for clicks and keys.
        connect(s.fader, &QSlider::sliderReleased, this, [this, id, f = s.fader] { commitGain(id, f->value()); });
        connect(s.fader, &QSlider::valueChanged, this, [this, id, f = s.fader](int v) {
            if (!updating_ && !f->isSliderDown()) commitGain(id, v);
        });
        connect(s.pan, &QDial::sliderReleased, this, [this, id, d = s.pan] { commitPan(id, d->value()); });
        connect(s.pan, &QDial::valueChanged, this, [this, id, d = s.pan](int v) {
            if (!updating_ && !d->isSliderDown()) commitPan(id, v);
        });
        connect(s.mute, &QToolButton::clicked, this, [this, id] { toggle(id, true); });
        connect(s.solo, &QToolButton::clicked, this, [this, id] { toggle(id, false); });
        connect(s.effects, &QPushButton::clicked, this, [this, id] { editEffects(id); });
        strips_.push_back(s);
    }
    refresh();
}

void MixerPanel::refresh() {
    if (!session_) return;
    const auto ids = session_->timeline().trackIdsOfKind(TrackKind::Audio);
    const bool sameTracks = ids.size() == strips_.size() &&
                            std::equal(ids.begin(), ids.end(), strips_.begin(),
                                       [](const std::string& id, const Strip& s) { return s.trackId == id; });
    if (!sameTracks) {
        rebuild();
        return;
    }
    updating_ = true;
    for (Strip& s : strips_) {
        const Track* t = session_->timeline().track(s.trackId);
        s.name->setText(QString::fromStdString(t->name));
        s.fader->setValue(static_cast<int>(std::lround(t->gainDb * 10)));
        s.gainLabel->setText(QString("%1 dB").arg(t->gainDb, 0, 'f', 1));
        s.pan->setValue(static_cast<int>(std::lround(t->pan * 100)));
        s.pan->setToolTip(t->pan == 0 ? tr("Pan: centre")
                                      : tr("Pan: %1% %2").arg(std::abs(t->pan * 100), 0, 'f', 0).arg(t->pan < 0 ? tr("left") : tr("right")));
        s.mute->setChecked(t->muted);
        s.solo->setChecked(t->solo);
        const auto active = std::count_if(t->effects.begin(), t->effects.end(), [](const audio::EffectSpec& e) { return e.enabled; });
        s.effects->setText(t->effects.empty() ? tr("FX") : tr("FX (%1)").arg(active));
    }
    updating_ = false;
}

void MixerPanel::commitGain(const std::string& trackId, int tenthsDb) {
    const Track* t = session_ ? session_->timeline().track(trackId) : nullptr;
    if (!t) return;
    TrackState s = TrackState::of(*t);
    if (std::lround(s.gainDb * 10) == tenthsDb) return;
    s.gainDb = tenthsDb / 10.0;
    if (Status st = session_->setTrackState(trackId, s); !st.ok()) emit errorOccurred(QString::fromStdString(st.error().message));
}

void MixerPanel::commitPan(const std::string& trackId, int percent) {
    const Track* t = session_ ? session_->timeline().track(trackId) : nullptr;
    if (!t) return;
    TrackState s = TrackState::of(*t);
    if (std::lround(s.pan * 100) == percent) return;
    s.pan = percent / 100.0;
    if (Status st = session_->setTrackState(trackId, s); !st.ok()) emit errorOccurred(QString::fromStdString(st.error().message));
}

void MixerPanel::toggle(const std::string& trackId, bool mute) {
    const Track* t = session_ ? session_->timeline().track(trackId) : nullptr;
    if (!t) return;
    TrackState s = TrackState::of(*t);
    (mute ? s.muted : s.solo) = !(mute ? s.muted : s.solo);
    if (Status st = session_->setTrackState(trackId, s); !st.ok()) emit errorOccurred(QString::fromStdString(st.error().message));
}

void MixerPanel::editEffects(const std::string& trackId) {
    EffectsDialog dialog(session_, trackId, this);
    connect(&dialog, &EffectsDialog::errorOccurred, this, &MixerPanel::errorOccurred);
    dialog.exec();
}

void MixerPanel::showMeters(const render::MixMeters& meters) {
    for (Strip& s : strips_) {
        const auto it = meters.tracks.find(s.trackId);
        if (it != meters.tracks.end()) s.meter->setLevels(it->second.peakLeft, it->second.peakRight);
        else s.meter->setLevels(0, 0);
    }
    master_->setLevels(meters.master.peakLeft, meters.master.peakRight);
}

void MixerPanel::tick() {
    if (!isVisible()) return;
    std::optional<render::MixMeters> levels = viewer_ ? viewer_->meters() : std::nullopt;
    showMeters(levels.value_or(render::MixMeters{}));  // silence lets the meters fall
}

}  // namespace up::ui
