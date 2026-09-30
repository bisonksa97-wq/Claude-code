#include "render/Compositing.h"

#include <algorithm>
#include <cmath>

namespace up::render {
namespace {

constexpr double kPi = 3.14159265358979323846;

inline void blendPixel(uint8_t* dst, const uint8_t* src, double alpha) {
    const double a = alpha * src[3] / 255.0;
    if (a <= 0.0) return;
    for (int c = 0; c < 3; ++c) dst[c] = static_cast<uint8_t>(std::lround(src[c] * a + dst[c] * (1.0 - a)));
    dst[3] = 255;
}

// Bilinear sample at continuous source coordinates (pixel centres at +0.5), clamped to the image.
inline void sample(const VideoFrame& img, double x, double y, uint8_t out[4]) {
    const double fx = std::clamp(x - 0.5, 0.0, static_cast<double>(img.width - 1));
    const double fy = std::clamp(y - 0.5, 0.0, static_cast<double>(img.height - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, img.width - 1);
    const int y1 = std::min(y0 + 1, img.height - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    const uint8_t* p00 = img.row(y0) + x0 * 4;
    const uint8_t* p10 = img.row(y0) + x1 * 4;
    const uint8_t* p01 = img.row(y1) + x0 * 4;
    const uint8_t* p11 = img.row(y1) + x1 * 4;
    for (int c = 0; c < 4; ++c) {
        const double top = p00[c] + (p10[c] - p00[c]) * tx;
        const double bottom = p01[c] + (p11[c] - p01[c]) * tx;
        out[c] = static_cast<uint8_t>(std::lround(top + (bottom - top) * ty));
    }
}

}  // namespace

void compositeOver(VideoFrame& canvas, const VideoFrame& source, const LayerPlacement& p) {
    if (canvas.empty() || source.empty() || p.opacity <= 0.0 || p.scale <= 0.0) return;
    const double opacity = std::min(1.0, p.opacity);
    // Visible source region after cropping, in source pixels.
    const double sx0 = std::clamp(p.cropLeft, 0.0, 1.0) * source.width;
    const double sx1 = source.width * (1.0 - std::clamp(p.cropRight, 0.0, 1.0));
    const double sy0 = std::clamp(p.cropTop, 0.0, 1.0) * source.height;
    const double sy1 = source.height * (1.0 - std::clamp(p.cropBottom, 0.0, 1.0));
    if (sx1 <= sx0 || sy1 <= sy0) return;

    const double rotation = std::fmod(p.rotationDegrees, 360.0);
    if (std::abs(rotation) < 1e-9 && std::abs(p.scale - 1.0) < 1e-9) {
        // Fast path: 1:1 copy/blend of the cropped rows.
        const int left = static_cast<int>(std::lround(p.centerX - source.width / 2.0));
        const int top = static_cast<int>(std::lround(p.centerY - source.height / 2.0));
        const int cx0 = std::max(0, left + static_cast<int>(std::ceil(sx0 - 1e-9)));
        const int cx1 = std::min(canvas.width, left + static_cast<int>(std::floor(sx1 + 1e-9)));
        const int cy0 = std::max(0, top + static_cast<int>(std::ceil(sy0 - 1e-9)));
        const int cy1 = std::min(canvas.height, top + static_cast<int>(std::floor(sy1 + 1e-9)));
        for (int y = cy0; y < cy1; ++y) {
            uint8_t* dst = canvas.row(y);
            const uint8_t* src = source.row(y - top);
            for (int x = cx0; x < cx1; ++x) blendPixel(dst + x * 4, src + (x - left) * 4, opacity);
        }
        return;
    }

    // General path: inverse-map each canvas pixel inside the layer's bounding box.
    const double radians = rotation * kPi / 180.0;
    const double cosA = std::cos(radians);
    const double sinA = std::sin(radians);
    const double halfW = source.width / 2.0;
    const double halfH = source.height / 2.0;
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (double cx : {sx0, sx1})
        for (double cy : {sy0, sy1}) {
            const double lx = (cx - halfW) * p.scale;
            const double ly = (cy - halfH) * p.scale;
            const double X = p.centerX + lx * cosA - ly * sinA;
            const double Y = p.centerY + lx * sinA + ly * cosA;
            minX = std::min(minX, X);
            maxX = std::max(maxX, X);
            minY = std::min(minY, Y);
            maxY = std::max(maxY, Y);
        }
    const int bx0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int bx1 = std::min(canvas.width, static_cast<int>(std::ceil(maxX)));
    const int by0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int by1 = std::min(canvas.height, static_cast<int>(std::ceil(maxY)));
    uint8_t texel[4];
    for (int y = by0; y < by1; ++y) {
        uint8_t* dst = canvas.row(y);
        const double dy = y + 0.5 - p.centerY;
        for (int x = bx0; x < bx1; ++x) {
            const double dx = x + 0.5 - p.centerX;
            // Inverse rotation, then inverse scale, back to source coordinates.
            const double sx = (dx * cosA + dy * sinA) / p.scale + halfW;
            const double sy = (-dx * sinA + dy * cosA) / p.scale + halfH;
            if (sx < sx0 || sx >= sx1 || sy < sy0 || sy >= sy1) continue;
            sample(source, sx, sy, texel);
            blendPixel(dst + x * 4, texel, opacity);
        }
    }
}

}  // namespace up::render
