#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "timeline/Animation.h"

namespace up {

// Primary colour-correction parameters of a clip (all keyframable).
//
// Order of operations (see render/ColorGrading.h):
//   linear light:  exposure (stops), then white balance (temperature, tint)
//   encoded (display) values: offset, lift, gain, gamma, contrast around pivot,
//   saturation (Rec.709 luma preserved), clamp to 0..1.
// Lift/gamma/gain/offset each have a master and per-channel value: lift and offset
// add, gain and gamma multiply.
enum class GradeParam {
    LiftMaster, LiftR, LiftG, LiftB,
    GammaMaster, GammaR, GammaG, GammaB,
    GainMaster, GainR, GainG, GainB,
    OffsetMaster, OffsetR, OffsetG, OffsetB,
    Contrast, Pivot, Saturation, Exposure, Temperature, Tint,
};

inline constexpr std::size_t kGradeParamCount = 22;

struct GradeParamInfo {
    const char* id;
    const char* label;
    const char* group;  // "Lift", "Gamma", "Gain", "Offset", "Basic"
    double defaultValue;
    double minimum;
    double maximum;
};

const GradeParamInfo& gradeInfo(GradeParam param);
std::optional<GradeParam> gradeParamFromString(const std::string& id);

// Custom curves (not animated). Every curve maps 0..1 to 0..1 through its control
// points with monotone cubic (Fritsch-Carlson) interpolation, so it never overshoots
// between points; an empty curve is the identity.
//   Master, Red, Green, Blue: tone curves on encoded values after the primaries,
//     flat outside the first/last point; at least two points when not empty.
//   HueVsHue, HueVsSat: x = hue (vectorscope angle, red = 0, then yellow, green,
//     cyan, blue, magenta), wrapping around; y = 0.5 is neutral.
//     HueVsHue rotates the hue by (y - 0.5) turns; HueVsSat multiplies chroma by 2y.
//   LumVsSat: x = Rec.709 luma; multiplies chroma by 2y.
enum class CurveKind { Master, Red, Green, Blue, HueVsHue, HueVsSat, LumVsSat };
inline constexpr std::size_t kCurveKindCount = 7;
inline constexpr std::size_t kMaxCurvePoints = 32;

struct CurvePoint {
    double x = 0.0;
    double y = 0.0;
    bool operator==(const CurvePoint&) const = default;
};

const char* curveId(CurveKind kind);   // "master", "red", ..., "lumVsSat"
const char* curveLabel(CurveKind kind);
std::optional<CurveKind> curveKindFromString(const std::string& id);
bool isHueCurve(CurveKind kind);  // wraps around (HueVsHue, HueVsSat)
bool isToneCurve(CurveKind kind);  // Master, Red, Green, Blue
// Validates and sorts points by x: all finite and within 0..1, distinct x, at most
// kMaxCurvePoints, tone curves empty or with at least two points.
std::optional<std::string> validateCurve(CurveKind kind, std::vector<CurvePoint>& points);

// A 3D (or 1D) LUT file used by a grade or as a timeline output transform. Like media
// it is referenced, never embedded: `path` is the last known absolute location and
// `relativePath` is written relative to the project file so moved project folders keep
// working.
struct LutRef {
    std::filesystem::path path;
    std::filesystem::path relativePath;
    bool operator==(const LutRef& o) const { return path == o.path; }
};

struct ClipGrade {
    std::array<AnimatedValue, kGradeParamCount> values = defaults();
    std::array<std::vector<CurvePoint>, kCurveKindCount> curves{};
    std::optional<LutRef> lut;  // applied last, after the curves

    std::vector<CurvePoint>& curve(CurveKind k) { return curves[static_cast<std::size_t>(k)]; }
    const std::vector<CurvePoint>& curve(CurveKind k) const { return curves[static_cast<std::size_t>(k)]; }

    AnimatedValue& operator[](GradeParam p) { return values[static_cast<std::size_t>(p)]; }
    const AnimatedValue& operator[](GradeParam p) const { return values[static_cast<std::size_t>(p)]; }
    bool isIdentity() const;
    static std::array<AnimatedValue, kGradeParamCount> defaults();
};

// A stored, inactive grade version of a clip (see Clip::gradeVersions).
struct NamedGrade {
    std::string name;
    ClipGrade grade;
};

}  // namespace up
