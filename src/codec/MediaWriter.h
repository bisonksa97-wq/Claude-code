#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "codec/VideoFrame.h"
#include "core/Rational.h"
#include "core/Result.h"

namespace up {

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
// output file extension (.mp4, .mov, .mkv ...).
class MediaWriter {
public:
    static Result<std::unique_ptr<MediaWriter>> open(const std::filesystem::path& path, const EncodeSettings& settings);
    ~MediaWriter();

    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;

    // Frames must match the configured size and are timestamped sequentially.
    Status writeVideo(const VideoFrame& frame);
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
