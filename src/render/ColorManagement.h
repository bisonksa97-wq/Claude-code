#pragma once

#include <array>

#include "codec/VideoFrame.h"
#include "render/FloatFrame.h"
#include "timeline/ColorSpace.h"

namespace up::render {

// Transfer functions (see timeline/ColorSpace.h for the linear normalisation).
// Power-law curves are mirrored for negative values so conversions stay invertible.
double decodeTransfer(Transfer transfer, double encoded);  // encoded -> linear
double encodeTransfer(Transfer transfer, double linear);   // linear -> encoded

using Matrix3 = std::array<std::array<double, 3>, 3>;

// CIE 1931 xy of the red, green and blue primaries and the (D65) white point.
std::array<std::array<double, 2>, 4> primariesChromaticities(Primaries primaries);

// RGB -> CIE XYZ for the primaries (D65 white, Y of white = 1).
Matrix3 rgbToXyz(Primaries primaries);
// Linear RGB in `from` primaries -> linear RGB in `to` primaries (all spaces share D65,
// so no chromatic adaptation is needed).
Matrix3 primariesConversion(Primaries from, Primaries to);

// Converts pixels from one colour space to another: decode the source transfer,
// convert primaries in linear light, encode the destination transfer. No tone or
// gamut mapping is applied: out-of-range values are kept (float) until quantisation.
class ColorConversion {
public:
    ColorConversion(ColorSpace from, ColorSpace to);

    bool isIdentity() const { return identity_; }
    std::array<float, 3> apply(float r, float g, float b) const;
    void apply(FloatFrame& frame) const;
    // 8-bit input: decoding runs through an exact 256-entry table.
    FloatFrame convert(const VideoFrame& frame) const;
    FloatFrame convert(const VideoFrame16& frame) const;

private:
    std::array<float, 3> fromLinear(double r, double g, double b) const;

    ColorSpace from_;
    ColorSpace to_;
    Matrix3 matrix_{};
    bool identity_ = true;
    bool sameTransfer_ = true;
    bool samePrimaries_ = true;
};

}  // namespace up::render
