#include "render/ViewerOverlay.h"

#include <array>
#include <cmath>

namespace up::render {
namespace {

void set(uint8_t* px, uint8_t r, uint8_t g, uint8_t b) {
    px[0] = r;
    px[1] = g;
    px[2] = b;
}

}  // namespace

void applyOverlay(VideoFrame& frame, ViewerOverlay overlay) {
    if (overlay == ViewerOverlay::None) return;
    for (std::size_t i = 0; i + 3 < frame.pixels.size(); i += 4) {
        uint8_t* px = frame.pixels.data() + i;
        if (overlay == ViewerOverlay::Clipping) {
            if (px[0] >= 254 || px[1] >= 254 || px[2] >= 254) set(px, 255, 0, 0);
            else if (px[0] <= 1 && px[1] <= 1 && px[2] <= 1) set(px, 0, 80, 255);
            continue;
        }
        const double percent = (0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]) / 255.0 * 100.0;
        if (percent < 2.5) set(px, 128, 0, 160);
        else if (percent < 4.0) set(px, 0, 60, 255);
        else if (percent >= 38.0 && percent < 42.0) set(px, 0, 200, 0);
        else if (percent >= 52.0 && percent < 56.0) set(px, 255, 140, 180);
        else if (percent >= 97.0 && percent <= 99.0) set(px, 255, 230, 0);
        else if (percent > 99.0) set(px, 255, 0, 0);
        else {
            const auto grey = static_cast<uint8_t>(std::lround(percent / 100.0 * 255.0));
            set(px, grey, grey, grey);
        }
    }
}

}  // namespace up::render
