#include "ui/ColorPanel.h"

#include <QConicalGradient>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "app/EditorSession.h"
#include "ui/Theme.h"

namespace up::ui {
namespace {

constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;

struct WheelGroup {
    const char* name;
    GradeParam master;
    double range;  // channel offset at the wheel's rim
};

constexpr std::array<WheelGroup, 4> kWheels = {{
    {"Lift", GradeParam::LiftMaster, 0.25},
    {"Gamma", GradeParam::GammaMaster, 0.5},
    {"Gain", GradeParam::GainMaster, 0.5},
    {"Offset", GradeParam::OffsetMaster, 0.25},
}};

GradeParam channelParam(GradeParam master, int channel) {
    return static_cast<GradeParam>(static_cast<int>(master) + 1 + channel);
}

// Angle (degrees, counter-clockwise from +Cb) of an RGB colour on the vectorscope.
double scopeAngle(double r, double g, double b) {
    const double y = kLumaR * r + kLumaG * g + kLumaB * b;
    return std::atan2((r - y) / 1.5748, (b - y) / 1.8556) * 180.0 / std::numbers::pi;
}

}  // namespace

std::array<double, 3> wheelToChannelOffsets(double x, double y, double range) {
    const double cb = x * range;
    const double cr = y * range;
    const double r = 1.5748 * cr;
    const double b = 1.8556 * cb;
    const double g = -(kLumaR * r + kLumaB * b) / kLumaG;  // zero luma
    return {r, g, b};
}

std::array<double, 2> channelOffsetsToWheel(const std::array<double, 3>& d, double range) {
    const double y = kLumaR * d[0] + kLumaG * d[1] + kLumaB * d[2];
    return {(d[2] - y) / 1.8556 / range, (d[0] - y) / 1.5748 / range};
}

// --- ColorWheel ------------------------------------------------------------------------

ColorWheel::ColorWheel(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Drag to shift the colour balance; double-click to reset"));
}

void ColorWheel::setBalance(double x, double y) {
    if (dragging_) return;  // the model catches up on release
    const double len = std::hypot(x, y);
    if (len > 1.0) {
        x /= len;
        y /= len;
    }
    x_ = x;
    y_ = y;
    update();
}

void ColorWheel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const DesignTokens& t = currentTokens();
    const double radius = std::min(width(), height()) / 2.0 - 4.0;
    const QPointF centre(width() / 2.0, height() / 2.0);
    if (radius <= 0) return;

    // Hue ring at the vectorscope angles of the primaries and secondaries.
    QConicalGradient hue(centre, 0);
    struct Stop {
        double angle;
        QColor colour;
    };
    std::array<Stop, 6> stops = {{{scopeAngle(1, 0, 0), QColor(255, 0, 0)},
                                  {scopeAngle(1, 1, 0), QColor(255, 255, 0)},
                                  {scopeAngle(0, 1, 0), QColor(0, 255, 0)},
                                  {scopeAngle(0, 1, 1), QColor(0, 255, 255)},
                                  {scopeAngle(0, 0, 1), QColor(0, 0, 255)},
                                  {scopeAngle(1, 0, 1), QColor(255, 0, 255)}}};
    for (auto& s : stops) s.angle = std::fmod(s.angle + 360.0, 360.0);
    std::sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.angle < b.angle; });
    // Colour at 0/360 degrees, interpolated between the last and first stop.
    const Stop& last = stops.back();
    const Stop& first = stops.front();
    const double span = first.angle + 360.0 - last.angle;
    const double f = span > 0 ? (360.0 - last.angle) / span : 0.0;
    auto mix = [](const QColor& a, const QColor& b, double k) {
        return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * k),
                                static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * k),
                                static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * k));
    };
    const QColor wrap = mix(last.colour, first.colour, f);
    hue.setColorAt(0.0, wrap);
    for (const auto& s : stops) hue.setColorAt(s.angle / 360.0, s.colour);
    hue.setColorAt(1.0, wrap);
    p.setPen(QPen(t.border, 1));
    p.setBrush(hue);
    p.drawEllipse(centre, radius, radius);
    QRadialGradient fade(centre, radius);
    QColor inner = t.panel;
    QColor outer = t.panel;
    outer.setAlphaF(0.35f);
    fade.setColorAt(0.0, inner);
    fade.setColorAt(1.0, outer);
    p.setBrush(fade);
    p.drawEllipse(centre, radius, radius);

    p.setPen(QPen(t.textMuted, 1, Qt::DotLine));
    p.drawLine(QPointF(centre.x() - radius, centre.y()), QPointF(centre.x() + radius, centre.y()));
    p.drawLine(QPointF(centre.x(), centre.y() - radius), QPointF(centre.x(), centre.y() + radius));

    const QPointF puck(centre.x() + x_ * radius, centre.y() - y_ * radius);
    p.setPen(QPen(t.text, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(puck, 5, 5);
    if (!isEnabled()) p.fillRect(rect(), QColor(t.panel.red(), t.panel.green(), t.panel.blue(), 140));
}

void ColorWheel::moveTo(const QPointF& pos) {
    const double radius = std::min(width(), height()) / 2.0 - 4.0;
    if (radius <= 0) return;
    double x = (pos.x() - width() / 2.0) / radius;
    double y = -(pos.y() - height() / 2.0) / radius;
    const double len = std::hypot(x, y);
    if (len > 1.0) {
        x /= len;
        y /= len;
    }
    x_ = x;
    y_ = y;
    update();
}

void ColorWheel::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
    moveTo(event->position());
}

void ColorWheel::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) moveTo(event->position());
}

void ColorWheel::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    moveTo(event->position());
    emit balanceCommitted(x_, y_);
}

void ColorWheel::mouseDoubleClickEvent(QMouseEvent*) {
    dragging_ = false;
    emit resetRequested();
}

// --- ColorPanel ------------------------------------------------------------------------

ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);

    title_ = new QLabel(content);
    title_->setWordWrap(true);
    layout->addWidget(title_);

    form_ = new QWidget(content);
    auto* formLayout = new QVBoxLayout(form_);
    formLayout->setContentsMargins(0, 0, 0, 0);

    auto* wheelRow = new QHBoxLayout;
    for (std::size_t i = 0; i < kWheels.size(); ++i) {
        auto* column = new QVBoxLayout;
        auto* wheel = new ColorWheel(form_);
        wheel->setAccessibleName(tr("%1 balance").arg(tr(kWheels[i].name)));
        wheels_[i] = wheel;
        column->addWidget(wheel, 1);
        auto* caption = new QLabel(tr(kWheels[i].name), form_);
        caption->setAlignment(Qt::AlignCenter);
        column->addWidget(caption);
        wheelRow->addLayout(column);
        const int index = static_cast<int>(i);
        connect(wheel, &ColorWheel::balanceCommitted, this, [this, index](double x, double y) { commitWheel(index, x, y); });
        connect(wheel, &ColorWheel::resetRequested, this, [this, index] { commitWheel(index, 0.0, 0.0); });
    }
    formLayout->addLayout(wheelRow);

    auto* grid = new QGridLayout;
    int r = 0;
    const char* group = "";
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const GradeParam p = static_cast<GradeParam>(i);
        const GradeParamInfo& info = gradeInfo(p);
        if (std::string(info.group) != group) {
            group = info.group;
            auto* header = new QLabel(QString("<b>%1</b>").arg(tr(group)), form_);
            grid->addWidget(header, r++, 0, 1, 4);
        }
        Row& row = rows_[i];
        row.label = new QLabel(tr(info.label), form_);
        row.value = new QDoubleSpinBox(form_);
        const double span = info.maximum - info.minimum;
        row.value->setRange(info.minimum, info.maximum);
        row.value->setDecimals(span > 50 ? 1 : 3);
        row.value->setSingleStep(span > 50 ? 1.0 : span > 10 ? 0.1 : 0.01);
        row.value->setKeyboardTracking(false);
        row.value->setAccessibleName(tr(info.label));
        row.label->setBuddy(row.value);
        row.key = new QToolButton(form_);
        row.key->setText("◆");
        row.key->setCheckable(true);
        row.key->setToolTip(tr("Keyframe at playhead"));
        row.key->setAccessibleName(tr("Keyframe %1").arg(tr(info.label)));
        row.reset = new QToolButton(form_);
        row.reset->setText("↺");
        row.reset->setToolTip(tr("Reset"));
        row.reset->setAccessibleName(tr("Reset %1").arg(tr(info.label)));
        grid->addWidget(row.label, r, 0);
        grid->addWidget(row.value, r, 1);
        grid->addWidget(row.key, r, 2);
        grid->addWidget(row.reset, r, 3);
        ++r;
        connect(row.value, &QDoubleSpinBox::valueChanged, this, [this, p] {
            if (!updating_) commitValue(p);
        });
        connect(row.key, &QToolButton::clicked, this, [this, p] { toggleKey(p); });
        connect(row.reset, &QToolButton::clicked, this, [this, p] {
            if (session_) report(session_->resetGrade(clipId_, p));
        });
    }
    formLayout->addLayout(grid);

    auto* buttons = new QHBoxLayout;
    copy_ = new QPushButton(tr("Copy Grade"), form_);
    paste_ = new QPushButton(tr("Paste Grade"), form_);
    paste_->setToolTip(tr("Paste the copied grade onto every selected video clip"));
    reset_ = new QPushButton(tr("Reset Grade"), form_);
    buttons->addWidget(copy_);
    buttons->addWidget(paste_);
    buttons->addWidget(reset_);
    formLayout->addLayout(buttons);
    connect(copy_, &QPushButton::clicked, this, [this] {
        if (!session_) return;
        report(session_->copyGrade(clipId_));
        refresh();  // enables Paste
    });
    connect(paste_, &QPushButton::clicked, this, &ColorPanel::pasteRequested);
    connect(reset_, &QPushButton::clicked, this, [this] {
        if (session_) report(session_->resetGrade(clipId_));
    });

    layout->addWidget(form_);
    layout->addStretch(1);
    refresh();
}

void ColorPanel::setSession(EditorSession* session) {
    session_ = session;
    clipId_.clear();
    refresh();
}

void ColorPanel::setClip(const std::string& clipId) {
    clipId_.clear();
    if (session_ && !clipId.empty() && session_->timeline().clip(clipId)) clipId_ = clipId;
    refresh();
}

void ColorPanel::setPlayhead(FrameIndex frame) {
    playhead_ = frame;
    refresh();
}

void ColorPanel::refresh() {
    const Clip* clip = session_ && !clipId_.empty() ? session_->timeline().clip(clipId_) : nullptr;
    const bool video = clip && session_->timeline().trackOfClip(clipId_)->kind == TrackKind::Video;
    form_->setEnabled(video);
    paste_->setEnabled(video && session_->hasCopiedGrade());
    if (!video) {
        title_->setText(clip ? tr("%1 is an audio clip; select a video clip to grade it.").arg(QString::fromStdString(clip->name))
                             : tr("Select a video clip to grade it."));
        return;
    }
    const bool inside = clip->contains(playhead_);
    title_->setText(inside ? QString::fromStdString(clip->name)
                           : tr("%1 — move the playhead onto the clip to set keyframes").arg(QString::fromStdString(clip->name)));
    const FrameIndex source = clip->toSource(std::clamp(playhead_, clip->start, clip->end() - 1));
    updating_ = true;
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const GradeParam p = static_cast<GradeParam>(i);
        const Row& row = rows_[i];
        const AnimatedValue& v = clip->grade[p];
        row.value->setValue(v.at(source));
        row.key->setChecked(inside && v.keyAt(source) != nullptr);
        row.key->setEnabled(inside);
        row.reset->setEnabled(v.animated() || v.value != gradeInfo(p).defaultValue);
        QFont f = row.value->font();
        f.setBold(v.animated());
        row.value->setFont(f);
    }
    for (std::size_t w = 0; w < kWheels.size(); ++w) {
        std::array<double, 3> d{};
        for (int c = 0; c < 3; ++c) {
            const GradeParam p = channelParam(kWheels[w].master, c);
            d[static_cast<std::size_t>(c)] = clip->grade[p].at(source) - gradeInfo(p).defaultValue;
        }
        const auto pos = channelOffsetsToWheel(d, kWheels[w].range);
        wheels_[w]->setBalance(pos[0], pos[1]);
    }
    updating_ = false;
}

void ColorPanel::commitWheel(int index, double x, double y) {
    const Clip* clip = session_ && !clipId_.empty() ? session_->timeline().clip(clipId_) : nullptr;
    if (!clip) return;
    const WheelGroup& group = kWheels[static_cast<std::size_t>(index)];
    const FrameIndex source = clip->toSource(std::clamp(playhead_, clip->start, clip->end() - 1));
    // Keep the channels' common (luma) component; the wheel only sets the balance.
    std::array<double, 3> current{};
    for (int c = 0; c < 3; ++c) {
        const GradeParam p = channelParam(group.master, c);
        current[static_cast<std::size_t>(c)] = clip->grade[p].at(source) - gradeInfo(p).defaultValue;
    }
    const double luma = kLumaR * current[0] + kLumaG * current[1] + kLumaB * current[2];
    const auto chroma = wheelToChannelOffsets(x, y, group.range);
    std::vector<std::pair<GradeParam, double>> values;
    for (int c = 0; c < 3; ++c) {
        const GradeParam p = channelParam(group.master, c);
        values.emplace_back(p, gradeInfo(p).defaultValue + luma + chroma[static_cast<std::size_t>(c)]);
    }
    report(session_->setGradeParameters(clipId_, values, playhead_, std::string(group.name) + " Balance"));
    refresh();
}

void ColorPanel::commitValue(GradeParam param) {
    if (!session_ || clipId_.empty()) return;
    report(session_->setGradeParameter(clipId_, param, rows_[static_cast<std::size_t>(param)].value->value(), playhead_));
}

void ColorPanel::toggleKey(GradeParam param) {
    if (!session_ || clipId_.empty()) return;
    report(session_->setGradeKeyframe(clipId_, param, playhead_, rows_[static_cast<std::size_t>(param)].key->isChecked()));
}

void ColorPanel::report(const Status& status) {
    if (!status.ok()) {
        emit errorOccurred(QString::fromStdString(status.error().message));
        refresh();
    }
}

}  // namespace up::ui
