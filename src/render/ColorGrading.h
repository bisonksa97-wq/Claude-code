#pragma once

#include <array>

#include "codec/VideoFrame.h"
#include "render/FloatFrame.h"
#include "render/Lut.h"
#include "timeline/ColorSpace.h"
#include "timeline/Grade.h"

namespace up::render {

// Grade parameters evaluated at one source frame.
struct GradeValues {
    std::array<double, kGradeParamCount> v{};
    double operator[](GradeParam p) const { return v[static_cast<std::size_t>(p)]; }
    double& operator[](GradeParam p) { return v[static_cast<std::size_t>(p)]; }
    bool isIdentity() const;
};

GradeValues evaluateGrade(const ClipGrade& grade, FrameIndex sourceFrame);
GradeValues defaultGrade();

// sRGB transfer functions (used for 8-bit Rec.709/sRGB material; no colour
// management yet, so both are treated as the sRGB curve).
double srgbToLinear(double encoded);
double linearToSrgb(double linear);

// The per-channel part of the grade (everything except saturation) for an encoded
// input value, returned unclamped. Exposure and white balance work in linear light
// decoded with `transfer` (the timeline's transfer function). Exposed for tests.
double gradeChannel(const GradeValues& g, int channel, double encoded, Transfer transfer = Transfer::SRGB);

using GradeCurves = std::array<std::vector<CurvePoint>, kCurveKindCount>;

// Applies a grade in place, in this order:
//   1. primaries (gradeChannel) then the master and per-channel tone curves, through
//      per-channel tables (exact for 8-bit input, interpolated otherwise; values
//      outside 0..1 are computed directly);
//   2. per pixel, in Rec.709 Y/Cb/Cr: saturation, lum-vs-sat, hue-vs-sat and
//      hue-vs-hue (luma is preserved);
//   3. the LUT, if any.
// The float version does not clamp: the renderer quantises once, at the very end.
void applyGrade(FloatFrame& frame, const GradeValues& grade, const GradeCurves& curves = {}, const Lut* lut = nullptr,
                Transfer transfer = Transfer::SRGB);
// 8-bit convenience (sRGB transfer): converts, grades, clamps and quantises.
void applyGrade(VideoFrame& frame, const GradeValues& grade, const GradeCurves& curves = {}, const Lut* lut = nullptr);

// Applies a LUT to every pixel (timeline output LUT). Alpha is unchanged.
void applyLut(VideoFrame& frame, const Lut& lut);
void applyLut(FloatFrame& frame, const Lut& lut);

}  // namespace up::render
