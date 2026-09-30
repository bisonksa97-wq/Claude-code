#pragma once

#include <array>

#include "codec/VideoFrame.h"
#include "render/Lut.h"
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
// input value 0..1, returned unclamped. Exposed for tests.
double gradeChannel(const GradeValues& g, int channel, double encoded);

using GradeCurves = std::array<std::vector<CurvePoint>, kCurveKindCount>;

// Applies a grade in place, in this order:
//   1. primaries (gradeChannel) then the master and per-channel tone curves, as
//      256-entry per-channel tables built for this call (exact for 8-bit input);
//   2. per pixel, in Rec.709 Y/Cb/Cr: saturation, lum-vs-sat, hue-vs-sat and
//      hue-vs-hue (luma is preserved);
//   3. the LUT, if any;
// and the result is clamped and quantised to 8 bits once, at the end.
void applyGrade(VideoFrame& frame, const GradeValues& grade, const GradeCurves& curves = {}, const Lut* lut = nullptr);

// Applies a LUT to every pixel (timeline output LUT). Alpha is unchanged.
void applyLut(VideoFrame& frame, const Lut& lut);

}  // namespace up::render
