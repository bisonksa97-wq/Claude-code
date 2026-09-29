#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

#include "codec/MediaWriter.h"
#include "core/Result.h"

namespace up::media {

// Deterministic generated media, used for tests, demos and golden-output comparisons.
struct SyntheticSpec {
    bool video = true;  // false = audio-only file (use an audio container such as .m4a)
    int width = 320;
    int height = 240;
    FrameRate frameRate{25, 1};
    FrameIndex frames = 50;
    enum class Pattern {
        Solid,      // every frame is `color`
        FrameRamp,  // grey level encodes the frame number: (frame * rampStep) % 256
        Bars,       // vertical colour bars with a moving marker
    } pattern = Pattern::Bars;
    std::array<uint8_t, 3> color{200, 30, 30};
    int rampStep = 8;
    bool audio = true;
    double toneHz = 440.0;  // 0 = silence
    float toneLevel = 0.25f;
    int sampleRate = 48000;
    std::string videoCodec;  // empty = default encoder
};

Status generateSyntheticMedia(const std::filesystem::path& path, const SyntheticSpec& spec);

}  // namespace up::media
