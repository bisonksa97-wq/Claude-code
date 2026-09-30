#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "codec/VideoFrame.h"
#include "core/Rational.h"
#include "core/Result.h"

namespace up {

// SMPTE ST 2086 mastering display colour volume and CTA-861.3 content light levels,
// written as stream side data (and, for libx265, into the bitstream).
struct HdrMetadata {
    // CIE 1931 xy chromaticities of the mastering display.
    std::array<double, 2> red{0.708, 0.292};
    std::array<double, 2> green{0.170, 0.797};
    std::array<double, 2> blue{0.131, 0.046};
    std::array<double, 2> white{0.3127, 0.3290};
    double minLuminance = 0.0001;  // cd/m²
    double maxLuminance = 1000.0;  // cd/m²
    int maxCll = 0;                // cd/m², 0 = not written
    int maxFall = 0;
};

struct EncodeSettings {
    bool video = true;  // false writes an audio-only file
    int width = 1920;
    int height = 1080;
    FrameRate frameRate{25, 1};
    // FFmpeg encoder name; empty selects defaultVideoEncoder().
    std::string videoCodec;
    // Quality for encoders that support CRF (libx264/libx265); ignored otherwise.
    int crf = 18;
    int64_t videoBitrate = 0;  // bits/s; 0 = encoder default / CRF
    // Encoder pixel format (FFmpeg name, e.g. "yuv420p10le", "yuv422p10le", "rgb24").
    std::string pixelFormat = "yuv420p";
    // Encoder private options (e.g. prores_ks "profile" = "3") and a container codec
    // tag (e.g. "hvc1" so HEVC in MP4 plays on Apple devices).
    std::map<std::string, std::string> codecOptions;
    std::string codecTag;
    std::optional<HdrMetadata> hdr;
    // Colour tags written to the stream (FFmpeg names; empty = unspecified). The
    // matrix also selects the RGB -> YUV coefficients (default BT.709). Limited range.
    std::string colorPrimaries = "bt709";
    std::string colorTransfer = "bt709";
    std::string colorMatrix = "bt709";

    bool audio = true;
    std::string audioCodec = "aac";
    int sampleRate = 48000;
    int channels = 2;
    int64_t audioBitrate = 192000;
};

// Best available H.264 encoder, falling back to MPEG-4 Part 2 when none is present.
std::string defaultVideoEncoder();
bool encoderAvailable(const std::string& name);

// Encodes RGBA frames and interleaved float audio into a container chosen from the
// output file extension (.mp4, .mov, .mkv, .wav ...). A path containing a printf
// pattern such as "frame_%06d.png" writes an image sequence.
class MediaWriter {
public:
    static Result<std::unique_ptr<MediaWriter>> open(const std::filesystem::path& path, const EncodeSettings& settings);
    ~MediaWriter();

    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;

    // Frames must match the configured size and are timestamped sequentially.
    Status writeVideo(const VideoFrame& frame);
    // 16-bit input for high-bit-depth pixel formats (8-bit frames work too, but lose precision).
    Status writeVideo(const VideoFrame16& frame);
    // Appends `frameCount` interleaved samples (settings.channels per frame).
    Status writeAudio(const float* samples, int64_t frameCount);
    // Flushes encoders and finalises the container. Must be called for a playable file.
    Status finish();

    const EncodeSettings& settings() const;

private:
    struct Impl;
    explicit MediaWriter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace up
