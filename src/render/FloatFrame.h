#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "codec/VideoFrame.h"
#include "render/Parallel.h"

namespace up::render {

// std::allocator that leaves floats uninitialised on resize: frames are always
// written in full before being read, and zeroing 30+ MB per frame costs real time.
template <typename T>
struct UninitializedAllocator : std::allocator<T> {
    template <typename U>
    struct rebind {
        using other = UninitializedAllocator<U>;
    };
    using std::allocator<T>::allocator;
    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        if constexpr (sizeof...(Args) == 0) ::new (static_cast<void*>(p)) U;
        else ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
};

// A float RGBA image (straight alpha, 4 floats per pixel, row stride = width * 4).
// This is the working format of the renderer: values are not clamped, so they can
// exceed 0..1 (HDR, log, wide-gamut conversions) until the final quantisation.
struct FloatFrame {
    int width = 0;
    int height = 0;
    std::vector<float, UninitializedAllocator<float>> pixels;

    FloatFrame() = default;
    // Pixels start undefined unless `fill` is called (every renderer path writes them all).
    FloatFrame(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h * 4) {}

    bool empty() const { return width <= 0 || height <= 0; }
    float* row(int y) { return pixels.data() + static_cast<std::size_t>(y) * width * 4; }
    const float* row(int y) const { return pixels.data() + static_cast<std::size_t>(y) * width * 4; }

    void fill(float r, float g, float b, float a = 1.0f) {
        parallelRows(height, static_cast<std::size_t>(width) * 4, [&](int y0, int y1) {
            const std::size_t end = static_cast<std::size_t>(y1) * width * 4;
            for (std::size_t i = static_cast<std::size_t>(y0) * width * 4; i < end; i += 4) {
                pixels[i] = r;
                pixels[i + 1] = g;
                pixels[i + 2] = b;
                pixels[i + 3] = a;
            }
        });
    }
};

inline FloatFrame toFloatFrame(const VideoFrame& in) {
    FloatFrame out(in.width, in.height);
    static const auto table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) t[static_cast<std::size_t>(i)] = static_cast<float>(i) / 255.0f;
        return t;
    }();
    parallelRows(in.height, static_cast<std::size_t>(in.width) * 4, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uint8_t* src = in.row(y);
            float* dst = out.row(y);
            for (int i = 0; i < in.width * 4; ++i) dst[i] = table[src[i]];
        }
    });
    return out;
}

// Clamps to 0..1 and rounds to the nearest 8-bit code (halves round up).
inline VideoFrame toVideoFrame(const FloatFrame& in) {
    VideoFrame out(in.width, in.height);
    parallelRows(in.height, static_cast<std::size_t>(in.width) * 4, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* src = in.row(y);
            uint8_t* dst = out.row(y);
            for (int i = 0; i < in.width * 4; ++i) {
                const float v = src[i] > 0.0f ? (src[i] < 1.0f ? src[i] : 1.0f) : 0.0f;  // NaN -> 0
                dst[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
            }
        }
    });
    return out;
}

inline FloatFrame toFloatFrame(const VideoFrame16& in) {
    FloatFrame out(in.width, in.height);
    parallelRows(in.height, static_cast<std::size_t>(in.width) * 4, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const uint16_t* src = in.row(y);
            float* dst = out.row(y);
            for (int i = 0; i < in.width * 4; ++i) dst[i] = static_cast<float>(src[i]) / 65535.0f;
        }
    });
    return out;
}

// Clamps to 0..1 and rounds to the nearest 16-bit code (for high-bit-depth exports).
inline VideoFrame16 toVideoFrame16(const FloatFrame& in) {
    VideoFrame16 out(in.width, in.height);
    parallelRows(in.height, static_cast<std::size_t>(in.width) * 4, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* src = in.row(y);
            uint16_t* dst = out.row(y);
            for (int i = 0; i < in.width * 4; ++i) {
                const float v = src[i] > 0.0f ? (src[i] < 1.0f ? src[i] : 1.0f) : 0.0f;  // NaN -> 0
                dst[i] = static_cast<uint16_t>(v * 65535.0f + 0.5f);
            }
        }
    });
    return out;
}

}  // namespace up::render
