#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "codec/VideoFrame.h"

namespace up::render {

// Video scopes computed from an 8-bit RGBA frame (display-referred, Rec.709 weights).
struct Scopes {
    // Histograms: 256 bins each.
    std::array<std::vector<uint32_t>, 3> histogramRgb;
    std::vector<uint32_t> histogramLuma;
    // Waveforms: `columns` x 256 counts (row-major by level, level 0 = black at row 0).
    int columns = 0;
    std::vector<uint32_t> waveformLuma;
    std::array<std::vector<uint32_t>, 3> parade;
    // Vectorscope: 256 x 256 counts of (Cb, Cr) mapped from -0.5..0.5 to 0..255; row = Cr.
    std::vector<uint32_t> vectorscope;
    // Summary statistics.
    std::array<uint8_t, 3> minimum{};
    std::array<uint8_t, 3> maximum{};
    double meanLuma = 0.0;
};

// `columns` is the waveform's horizontal resolution (pixels are binned by x).
Scopes computeScopes(const VideoFrame& frame, int columns = 256);

// Vectorscope coordinates (0..255 each) for an 8-bit RGB colour.
std::array<int, 2> vectorscopePosition(uint8_t r, uint8_t g, uint8_t b);

}  // namespace up::render
