#include "ui/ColorPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QConicalGradient>
#include <QFileDialog>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "app/EditorSession.h"
#include "render/ColorCurves.h"
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

// --- CurveEditor -----------------------------------------------------------------------

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Click to add a point, drag to move it, right-click to remove it, double-click to reset"));
}

void CurveEditor::setCurve(CurveKind kind, std::vector<CurvePoint> points) {
    if (dragged_ >= 0) return;  // the model catches up on release
    kind_ = kind;
    points_ = std::move(points);
    update();
}

QRectF CurveEditor::plotRect() const {
    const double stripe = isToneCurve(kind_) ? 0.0 : 8.0;  // hue or luma reference under the plot
    return QRectF(6, 6, std::max(10, width() - 12), std::max(10.0, height() - 12 - stripe));
}

QPointF CurveEditor::toWidget(const CurvePoint& p) const {
    const QRectF r = plotRect();
    return {r.left() + p.x * r.width(), r.bottom() - p.y * r.height()};
}

CurvePoint CurveEditor::fromWidget(const QPointF& pos) const {
    const QRectF r = plotRect();
    return {std::clamp((pos.x() - r.left()) / r.width(), 0.0, 1.0), std::clamp((r.bottom() - pos.y()) / r.height(), 0.0, 1.0)};
}

int CurveEditor::pointAt(const QPointF& pos) const {
    for (std::size_t i = 0; i < points_.size(); ++i) {
        const QPointF d = toWidget(points_[i]) - pos;
        if (d.x() * d.x() + d.y() * d.y() <= 49.0) return static_cast<int>(i);
    }
    return -1;
}

void CurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const DesignTokens& t = currentTokens();
    const QRectF r = plotRect();
    p.fillRect(rect(), t.panelAlt);
    p.fillRect(r, t.window);
    if (!isToneCurve(kind_)) {
        const QRectF stripe(r.left(), r.bottom() + 3, r.width(), 6);
        QLinearGradient g(stripe.topLeft(), stripe.topRight());
        if (kind_ == CurveKind::LumVsSat) {
            g.setColorAt(0, Qt::black);
            g.setColorAt(1, Qt::white);
        } else {
            // Hue stripe: sample hueOf around the vectorscope so it matches the curve's x axis.
            for (int i = 0; i <= 36; ++i) {
                const double angle = 2.0 * std::numbers::pi * i / 36.0;
                const double cb = 0.3 * std::cos(angle), cr = 0.3 * std::sin(angle);
                const double rr = 0.5 + 1.5748 * cr, bb = 0.5 + 1.8556 * cb;
                const double gg = (0.5 - kLumaR * rr - kLumaB * bb) / kLumaG;
                const QColor c = QColor::fromRgbF(static_cast<float>(std::clamp(rr, 0.0, 1.0)), static_cast<float>(std::clamp(gg, 0.0, 1.0)),
                                                  static_cast<float>(std::clamp(bb, 0.0, 1.0)));
                g.setColorAt(render::hueOf(cb, cr), c);
            }
        }
        p.fillRect(stripe, g);
    }
    p.setPen(QPen(t.border, 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    p.drawRect(r);
    // Neutral reference: the diagonal for tone curves, the middle line otherwise.
    p.setPen(QPen(t.textMuted, 1, Qt::DashLine));
    if (isToneCurve(kind_)) p.drawLine(r.bottomLeft(), r.topRight());
    else p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));

    QColor colour = t.text;
    if (kind_ == CurveKind::Red) colour = QColor(230, 70, 70);
    if (kind_ == CurveKind::Green) colour = QColor(70, 200, 90);
    if (kind_ == CurveKind::Blue) colour = QColor(90, 130, 240);
    std::vector<CurvePoint> valid = points_;
    if (!validateCurve(kind_, valid)) {
        const render::CurveEvaluator curve(kind_, valid);
        QPainterPath path;
        const int steps = std::max(2, static_cast<int>(r.width()));
        for (int i = 0; i <= steps; ++i) {
            const double x = static_cast<double>(i) / steps;
            const QPointF at = toWidget({x, curve(x)});
            if (i == 0) path.moveTo(at);
            else path.lineTo(at);
        }
        p.setPen(QPen(colour, 2));
        p.drawPath(path);
    }
    p.setBrush(t.panel);
    p.setPen(QPen(colour, 1.5));
    for (const auto& pt : points_) p.drawEllipse(toWidget(pt), 4, 4);
    if (!isEnabled()) p.fillRect(rect(), QColor(t.panel.red(), t.panel.green(), t.panel.blue(), 140));
}

void CurveEditor::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    const int hit = pointAt(pos);
    if (event->button() == Qt::RightButton) {
        if (hit < 0) return;
        points_.erase(points_.begin() + hit);
        if (isToneCurve(kind_) && points_.size() < 2) points_.clear();  // back to the identity
        update();
        emit curveCommitted(points_);
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (hit >= 0) {
        dragged_ = hit;
        changed_ = false;
        return;
    }
    if (points_.size() >= kMaxCurvePoints) return;
    // Start empty curves from their neutral shape so a first click bends rather than replaces it.
    if (points_.empty()) {
        if (isToneCurve(kind_)) points_ = {{0, 0}, {1, 1}};
        else if (isHueCurve(kind_)) for (int i = 0; i < 6; ++i) points_.push_back({i / 6.0, 0.5});
        else points_ = {{0, 0.5}, {0.5, 0.5}, {1, 0.5}};
    }
    CurvePoint added = fromWidget(pos);
    if (isHueCurve(kind_)) added.x = std::min(added.x, 0.999);
    const auto it = std::find_if(points_.begin(), points_.end(), [&](const CurvePoint& q) { return q.x >= added.x; });
    if (it != points_.end() && std::abs(it->x - added.x) < 0.01) {
        dragged_ = static_cast<int>(it - points_.begin());  // close to an existing x: move that one
    } else {
        dragged_ = static_cast<int>(points_.insert(it, added) - points_.begin());
    }
    changed_ = true;
    moveDragged(pos);
}

void CurveEditor::moveDragged(const QPointF& pos) {
    if (dragged_ < 0) return;
    const auto i = static_cast<std::size_t>(dragged_);
    CurvePoint p = fromWidget(pos);
    // Keep the points ordered: a point stays between its neighbours.
    const double lo = i > 0 ? points_[i - 1].x + 0.005 : 0.0;
    const double hi = i + 1 < points_.size() ? points_[i + 1].x - 0.005 : (isHueCurve(kind_) ? 0.999 : 1.0);
    p.x = std::clamp(p.x, lo, std::max(lo, hi));
    if (points_[i] == p) return;
    points_[i] = p;
    changed_ = true;
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* event) { moveDragged(event->position()); }

void CurveEditor::mouseReleaseEvent(QMouseEvent* event) {
    if (dragged_ < 0 || event->button() != Qt::LeftButton) return;
    moveDragged(event->position());
    dragged_ = -1;
    if (changed_) emit curveCommitted(points_);
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent*) {
    dragged_ = -1;
    points_.clear();
    update();
    emit curveCommitted(points_);
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

    auto* versionRow = new QHBoxLayout;
    versionRow->addWidget(new QLabel(tr("Version"), form_));
    versions_ = new QComboBox(form_);
    versions_->setAccessibleName(tr("Grade version"));
    versionRow->addWidget(versions_, 1);
    newVersion_ = new QPushButton(tr("New"), form_);
    newVersion_->setToolTip(tr("Keep the current grade as a version and continue on a copy"));
    deleteVersion_ = new QPushButton(tr("Delete"), form_);
    deleteVersion_->setToolTip(tr("Delete the version shown and switch to the most recently stored one"));
    bypass_ = new QCheckBox(tr("Bypass"), form_);
    bypass_->setToolTip(tr("Show this clip without its grade"));
    versionRow->addWidget(newVersion_);
    versionRow->addWidget(deleteVersion_);
    versionRow->addWidget(bypass_);
    formLayout->addLayout(versionRow);
    connect(versions_, &QComboBox::activated, this, [this](int index) {
        if (session_ && !updating_) report(session_->selectGradeVersion(clipId_, versions_->itemText(index).toStdString()));
    });
    connect(newVersion_, &QPushButton::clicked, this, [this] {
        const Clip* clip = session_ ? session_->timeline().clip(clipId_) : nullptr;
        if (!clip) return;
        // Next free letter name: A, B, C ... then "Version N".
        auto taken = [&](const std::string& n) {
            return n == clip->gradeVersion || std::any_of(clip->gradeVersions.begin(), clip->gradeVersions.end(),
                                                          [&](const NamedGrade& v) { return v.name == n; });
        };
        std::string name;
        for (char c = 'A'; c <= 'Z' && name.empty(); ++c)
            if (!taken(std::string(1, c))) name = std::string(1, c);
        for (int n = 27; name.empty(); ++n)
            if (!taken("Version " + std::to_string(n))) name = "Version " + std::to_string(n);
        report(session_->addGradeVersion(clipId_, name));
    });
    connect(deleteVersion_, &QPushButton::clicked, this, [this] {
        const Clip* clip = session_ ? session_->timeline().clip(clipId_) : nullptr;
        if (!clip || clip->gradeVersions.empty()) return;
        // Deletes the version being shown, switching to the most recently stored one (one undo step).
        const std::string doomed = clip->gradeVersion;
        const std::string next = clip->gradeVersions.back().name;
        Transaction tx(session_->history(), "Delete Grade Version");
        Status s = session_->selectGradeVersion(clipId_, next);
        if (s.ok()) s = session_->deleteGradeVersion(clipId_, doomed);
        if (s.ok()) tx.commit();
        report(s);
    });
    connect(bypass_, &QCheckBox::toggled, this, [this](bool on) {
        if (session_ && !updating_) report(session_->setGradeBypass(clipId_, on));
    });

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

    auto* curveHeader = new QHBoxLayout;
    curveHeader->addWidget(new QLabel(QString("<b>%1</b>").arg(tr("Curves")), form_));
    curveKind_ = new QComboBox(form_);
    curveKind_->setAccessibleName(tr("Curve"));
    for (std::size_t i = 0; i < kCurveKindCount; ++i) curveKind_->addItem(tr(curveLabel(static_cast<CurveKind>(i))));
    curveHeader->addWidget(curveKind_, 1);
    resetCurve_ = new QPushButton(tr("Reset Curve"), form_);
    curveHeader->addWidget(resetCurve_);
    formLayout->addLayout(curveHeader);
    curveEditor_ = new CurveEditor(form_);
    curveEditor_->setAccessibleName(tr("Curve editor"));
    formLayout->addWidget(curveEditor_);
    connect(curveKind_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(curveEditor_, &CurveEditor::curveCommitted, this, [this](const std::vector<CurvePoint>& points) {
        if (session_) report(session_->setGradeCurve(clipId_, curveEditor_->kind(), points));
    });
    connect(resetCurve_, &QPushButton::clicked, this, [this] {
        if (session_) report(session_->setGradeCurve(clipId_, curveEditor_->kind(), {}));
    });

    auto* lutRow = new QHBoxLayout;
    lutRow->addWidget(new QLabel(QString("<b>%1</b>").arg(tr("LUT")), form_));
    lutLabel_ = new QLabel(form_);
    lutLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lutRow->addWidget(lutLabel_, 1);
    loadLut_ = new QPushButton(tr("Load LUT…"), form_);
    clearLut_ = new QPushButton(tr("Clear"), form_);
    clearLut_->setAccessibleName(tr("Clear LUT"));
    lutRow->addWidget(loadLut_);
    lutRow->addWidget(clearLut_);
    formLayout->addLayout(lutRow);
    connect(loadLut_, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Load LUT"), {}, tr("Cube LUTs (*.cube);;All files (*)"));
        if (!file.isEmpty()) loadLut(file);
    });
    connect(clearLut_, &QPushButton::clicked, this, [this] {
        if (session_) report(session_->setGradeLut(clipId_, std::nullopt));
    });

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
    std::vector<QString> names{QString::fromStdString(clip->gradeVersion)};
    for (const auto& v : clip->gradeVersions) names.push_back(QString::fromStdString(v.name));
    std::sort(names.begin(), names.end());
    versions_->clear();
    for (const auto& n : names) versions_->addItem(n);
    versions_->setCurrentText(QString::fromStdString(clip->gradeVersion));
    deleteVersion_->setEnabled(!clip->gradeVersions.empty());
    bypass_->setChecked(clip->gradeBypass);
    const auto kind = static_cast<CurveKind>(std::max(0, curveKind_->currentIndex()));
    curveEditor_->setCurve(kind, clip->grade.curve(kind));
    resetCurve_->setEnabled(!clip->grade.curve(kind).empty());
    if (clip->grade.lut) {
        const QString file = QString::fromStdString(clip->grade.lut->path.string());
        const bool present = QFileInfo::exists(file);
        lutLabel_->setText(present ? QFileInfo(file).fileName()
                                   : tr("<span style='color:%1'>%2 (missing)</span>")
                                         .arg(currentTokens().warning.name(), QFileInfo(file).fileName().toHtmlEscaped()));
        lutLabel_->setToolTip(file);
    } else {
        lutLabel_->setText(tr("None"));
        lutLabel_->setToolTip({});
    }
    clearLut_->setEnabled(clip->grade.lut.has_value());
    updating_ = false;
}

void ColorPanel::loadLut(const QString& path) {
    if (session_) report(session_->setGradeLut(clipId_, std::filesystem::path(path.toStdString())));
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
