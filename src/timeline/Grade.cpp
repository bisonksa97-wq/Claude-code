#include "timeline/Grade.h"

#include <algorithm>
#include <cmath>

namespace up {

const GradeParamInfo& gradeInfo(GradeParam param) {
    static const std::array<GradeParamInfo, kGradeParamCount> info = {{
        {"liftMaster", "Lift", "Lift", 0.0, -1.0, 1.0},
        {"liftR", "Lift R", "Lift", 0.0, -1.0, 1.0},
        {"liftG", "Lift G", "Lift", 0.0, -1.0, 1.0},
        {"liftB", "Lift B", "Lift", 0.0, -1.0, 1.0},
        {"gammaMaster", "Gamma", "Gamma", 1.0, 0.1, 4.0},
        {"gammaR", "Gamma R", "Gamma", 1.0, 0.1, 4.0},
        {"gammaG", "Gamma G", "Gamma", 1.0, 0.1, 4.0},
        {"gammaB", "Gamma B", "Gamma", 1.0, 0.1, 4.0},
        {"gainMaster", "Gain", "Gain", 1.0, 0.0, 4.0},
        {"gainR", "Gain R", "Gain", 1.0, 0.0, 4.0},
        {"gainG", "Gain G", "Gain", 1.0, 0.0, 4.0},
        {"gainB", "Gain B", "Gain", 1.0, 0.0, 4.0},
        {"offsetMaster", "Offset", "Offset", 0.0, -1.0, 1.0},
        {"offsetR", "Offset R", "Offset", 0.0, -1.0, 1.0},
        {"offsetG", "Offset G", "Offset", 0.0, -1.0, 1.0},
        {"offsetB", "Offset B", "Offset", 0.0, -1.0, 1.0},
        {"contrast", "Contrast", "Basic", 1.0, 0.0, 4.0},
        {"pivot", "Pivot", "Basic", 0.435, 0.0, 1.0},
        {"saturation", "Saturation", "Basic", 1.0, 0.0, 4.0},
        {"exposure", "Exposure", "Basic", 0.0, -8.0, 8.0},
        {"temperature", "Temperature", "Basic", 0.0, -100.0, 100.0},
        {"tint", "Tint", "Basic", 0.0, -100.0, 100.0},
    }};
    return info[static_cast<std::size_t>(param)];
}

std::optional<GradeParam> gradeParamFromString(const std::string& id) {
    for (std::size_t i = 0; i < kGradeParamCount; ++i)
        if (id == gradeInfo(static_cast<GradeParam>(i)).id) return static_cast<GradeParam>(i);
    return std::nullopt;
}

std::array<AnimatedValue, kGradeParamCount> ClipGrade::defaults() {
    std::array<AnimatedValue, kGradeParamCount> out{};
    for (std::size_t i = 0; i < kGradeParamCount; ++i) out[i].value = gradeInfo(static_cast<GradeParam>(i)).defaultValue;
    return out;
}

namespace {

struct CurveInfo {
    const char* id;
    const char* label;
};

constexpr std::array<CurveInfo, kCurveKindCount> kCurves = {{
    {"master", "Master"}, {"red", "Red"}, {"green", "Green"}, {"blue", "Blue"},
    {"hueVsHue", "Hue vs Hue"}, {"hueVsSat", "Hue vs Sat"}, {"lumVsSat", "Lum vs Sat"},
}};

}  // namespace

const char* curveId(CurveKind kind) { return kCurves[static_cast<std::size_t>(kind)].id; }
const char* curveLabel(CurveKind kind) { return kCurves[static_cast<std::size_t>(kind)].label; }

std::optional<CurveKind> curveKindFromString(const std::string& id) {
    for (std::size_t i = 0; i < kCurveKindCount; ++i)
        if (id == kCurves[i].id) return static_cast<CurveKind>(i);
    return std::nullopt;
}

bool isHueCurve(CurveKind kind) { return kind == CurveKind::HueVsHue || kind == CurveKind::HueVsSat; }
bool isToneCurve(CurveKind kind) { return static_cast<int>(kind) <= static_cast<int>(CurveKind::Blue); }

std::optional<std::string> validateCurve(CurveKind kind, std::vector<CurvePoint>& points) {
    if (points.size() > kMaxCurvePoints) return "A curve can have at most " + std::to_string(kMaxCurvePoints) + " points.";
    for (const auto& p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0)
            return "Curve points must lie between 0 and 1.";
    }
    std::sort(points.begin(), points.end(), [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    for (std::size_t i = 1; i < points.size(); ++i)
        if (points[i].x - points[i - 1].x < 1e-4) return "Two curve points cannot share the same input value.";
    if (isToneCurve(kind) && points.size() == 1) return "A tone curve needs at least two points.";
    if (isHueCurve(kind) && !points.empty() && points.back().x - points.front().x > 1.0 - 1e-4)
        return "A hue curve wraps around: 0 and 1 are the same hue, so use only one of them.";
    return std::nullopt;
}

bool ClipGrade::isIdentity() const {
    if (lut) return false;
    for (const auto& c : curves)
        if (!c.empty()) return false;
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const AnimatedValue& v = values[i];
        if (v.animated() || v.value != gradeInfo(static_cast<GradeParam>(i)).defaultValue) return false;
    }
    return true;
}

}  // namespace up
