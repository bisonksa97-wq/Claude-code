#pragma once

#include <cstdint>
#include <vector>

namespace up {

// A CPU-side 8-bit RGBA image (row stride = width * 4, straight alpha).
struct VideoFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    VideoFrame() = default;
    VideoFrame(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h * 4, 0) {}

    bool empty() const { return width <= 0 || height <= 0; }
    uint8_t* row(int y) { return pixels.data() + static_cast<std::size_t>(y) * width * 4; }
    const uint8_t* row(int y) const { return pixels.data() + static_cast<std::size_t>(y) * width * 4; }

    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
};

// 16 bits per component RGBA (0..65535, straight alpha), for high-bit-depth sources
// and exports. Row stride = width * 4 components.
struct VideoFrame16 {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> pixels;

    VideoFrame16() = default;
    VideoFrame16(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h * 4, 0) {}

    bool empty() const { return width <= 0 || height <= 0; }
    uint16_t* row(int y) { return pixels.data() + static_cast<std::size_t>(y) * width * 4; }
    const uint16_t* row(int y) const { return pixels.data() + static_cast<std::size_t>(y) * width * 4; }
};

}  // namespace up
