#include "render/Scopes.h"

#include <algorithm>
#include <cmath>

namespace up::render {
namespace {

struct YCbCr {
    double y, cb, cr;
};

YCbCr toYCbCr(double r, double g, double b) {
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return {y, (b - y) / 1.8556, (r - y) / 1.5748};  // Rec.709, cb/cr in -0.5..0.5
}

int toBin(double v) { return std::clamp(static_cast<int>(std::lround((v + 0.5) * 255.0)), 0, 255); }

}  // namespace

std::array<int, 2> vectorscopePosition(uint8_t r, uint8_t g, uint8_t b) {
    const YCbCr c = toYCbCr(r / 255.0, g / 255.0, b / 255.0);
    return {toBin(c.cb), toBin(c.cr)};
}

Scopes computeScopes(const VideoFrame& frame, int columns) {
    Scopes s;
    columns = std::max(1, columns);
    s.columns = columns;
    for (auto& h : s.histogramRgb) h.assign(256, 0);
    s.histogramLuma.assign(256, 0);
    s.waveformLuma.assign(static_cast<std::size_t>(columns) * 256, 0);
    for (auto& p : s.parade) p.assign(static_cast<std::size_t>(columns) * 256, 0);
    s.vectorscope.assign(256 * 256, 0);
    s.minimum = {255, 255, 255};
    s.maximum = {0, 0, 0};
    if (frame.empty()) {
        s.minimum = {0, 0, 0};
        return s;
    }
    double lumaSum = 0.0;
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* row = frame.row(y);
        for (int x = 0; x < frame.width; ++x) {
            const uint8_t* px = row + x * 4;
            const int column = static_cast<int>(static_cast<int64_t>(x) * columns / frame.width);
            const YCbCr c = toYCbCr(px[0] / 255.0, px[1] / 255.0, px[2] / 255.0);
            const int luma = std::clamp(static_cast<int>(std::lround(c.y * 255.0)), 0, 255);
            lumaSum += c.y;
            ++s.histogramLuma[static_cast<std::size_t>(luma)];
            ++s.waveformLuma[static_cast<std::size_t>(luma) * columns + column];
            for (int ch = 0; ch < 3; ++ch) {
                ++s.histogramRgb[ch][px[ch]];
                ++s.parade[ch][static_cast<std::size_t>(px[ch]) * columns + column];
                s.minimum[ch] = std::min(s.minimum[ch], px[ch]);
                s.maximum[ch] = std::max(s.maximum[ch], px[ch]);
            }
            ++s.vectorscope[static_cast<std::size_t>(toBin(c.cr)) * 256 + toBin(c.cb)];
        }
    }
    s.meanLuma = lumaSum / (static_cast<double>(frame.width) * frame.height);
    return s;
}

}  // namespace up::render
