#pragma once

#include <filesystem>
#include <string>

#include "codec/VideoFrame.h"
#include "media/SyntheticMedia.h"

namespace up::test {

// A unique temporary directory removed on destruction.
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

// Generates synthetic media, failing the current test on error.
void makeMedia(const std::filesystem::path& path, const media::SyntheticSpec& spec);

media::SyntheticSpec solid(uint8_t r, uint8_t g, uint8_t b, FrameIndex frames, double toneHz = 440.0);

// Average RGB over the whole frame.
struct Rgb {
    double r = 0, g = 0, b = 0;
};
Rgb averageColor(const VideoFrame& frame);

}  // namespace up::test
