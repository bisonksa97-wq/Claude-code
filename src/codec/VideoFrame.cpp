#include "codec/VideoFrame.h"

namespace up {

void VideoFrame::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
}

}  // namespace up
