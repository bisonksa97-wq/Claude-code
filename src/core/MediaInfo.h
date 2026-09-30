#pragma once

#include <cstdint>
#include <string>

#include "core/Rational.h"

namespace up {

// Technical description of a media file, produced by the codec engine's probe
// and persisted in the project so offline media can still be displayed.
struct MediaInfo {
    std::string container;
    double durationSeconds = 0.0;
    int64_t fileSize = 0;

    bool hasVideo = false;
    std::string videoCodec;
    int width = 0;
    int height = 0;
    FrameRate frameRate{0, 1};
    std::string pixelFormat;
    bool isStill = false;
    int bitDepth = 8;  // bits per component of the video pixel format
    // Colour tags as FFmpeg names ("bt709", "smpte2084", ...); empty = unspecified.
    std::string colorPrimaries;
    std::string colorTransfer;
    std::string colorMatrix;
    std::string colorRange;  // "tv" (limited) or "pc" (full)
    // HDR10 static metadata when present (0 = absent).
    double masteringMaxLuminance = 0.0;  // cd/m²
    double masteringMinLuminance = 0.0;
    int maxCll = 0;
    int maxFall = 0;

    bool hasAudio = false;
    std::string audioCodec;
    int sampleRate = 0;
    int channels = 0;

    std::string timecode;  // start timecode tag, when present
};

}  // namespace up
