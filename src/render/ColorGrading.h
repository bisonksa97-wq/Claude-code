#pragma once

#include <array>

#include "codec/VideoFrame.h"
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

// Applies the grade in place. Per-channel operations run through 256-entry lookup
// tables built for this call (exact for 8-bit input); saturation is applied after.
void applyGrade(VideoFrame& frame, const GradeValues& grade);

}  // namespace up::render
