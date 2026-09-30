#pragma once

#include <array>
#include <optional>
#include <string>

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

struct ClipGrade {
    std::array<AnimatedValue, kGradeParamCount> values = defaults();

    AnimatedValue& operator[](GradeParam p) { return values[static_cast<std::size_t>(p)]; }
    const AnimatedValue& operator[](GradeParam p) const { return values[static_cast<std::size_t>(p)]; }
    bool isIdentity() const;
    static std::array<AnimatedValue, kGradeParamCount> defaults();
};

}  // namespace up
