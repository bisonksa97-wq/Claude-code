#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include "core/Id.h"

namespace up::test {

TempDir::TempDir() {
    path_ = std::filesystem::temp_directory_path() / ("up-test-" + generateId().substr(0, 12));
    std::filesystem::create_directories(path_);
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

void makeMedia(const std::filesystem::path& path, const media::SyntheticSpec& spec) {
    const Status s = media::generateSyntheticMedia(path, spec);
    ASSERT_TRUE(s.ok()) << s.error().toString();
}

media::SyntheticSpec solid(uint8_t r, uint8_t g, uint8_t b, FrameIndex frames, double toneHz) {
    media::SyntheticSpec spec;
    spec.pattern = media::SyntheticSpec::Pattern::Solid;
    spec.color = {r, g, b};
    spec.frames = frames;
    spec.width = 160;
    spec.height = 120;
    spec.toneHz = toneHz;
    return spec;
}

Rgb averageColor(const VideoFrame& frame) {
    Rgb out;
    const std::size_t n = frame.pixels.size() / 4;
    for (std::size_t i = 0; i < n; ++i) {
        out.r += frame.pixels[i * 4];
        out.g += frame.pixels[i * 4 + 1];
        out.b += frame.pixels[i * 4 + 2];
    }
    if (n > 0) {
        out.r /= static_cast<double>(n);
        out.g /= static_cast<double>(n);
        out.b /= static_cast<double>(n);
    }
    return out;
}

}  // namespace up::test
