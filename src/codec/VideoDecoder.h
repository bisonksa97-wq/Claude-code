#pragma once

#include <filesystem>
#include <memory>

#include "codec/VideoFrame.h"
#include "core/Result.h"

namespace up {

// Frame-accurate random-access video decoder for one file.
//
// `frameAt(t)` returns the frame being displayed at time `t` seconds (relative
// to the start of the media): the last frame whose presentation time is <= t.
// Sequential access decodes forward without seeking; jumps seek to the previous
// keyframe and decode up to the target. Not thread-safe: use one instance per thread.
class VideoDecoder {
public:
    static Result<std::unique_ptr<VideoDecoder>> open(const std::filesystem::path& path);
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    int width() const;
    int height() const;

    // Decodes the frame at `seconds` and converts it to RGBA at outWidth x outHeight
    // (0 = native size).
    Result<VideoFrame> frameAt(double seconds, int outWidth = 0, int outHeight = 0);

private:
    struct Impl;
    explicit VideoDecoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace up
