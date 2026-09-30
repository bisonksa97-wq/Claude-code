#include "timeline/Grade.h"

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

bool ClipGrade::isIdentity() const {
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const AnimatedValue& v = values[i];
        if (v.animated() || v.value != gradeInfo(static_cast<GradeParam>(i)).defaultValue) return false;
    }
    return true;
}

}  // namespace up
