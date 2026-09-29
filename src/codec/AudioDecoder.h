#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "core/Result.h"

namespace up {

// Decodes a file's audio into interleaved 32-bit float samples at a fixed
// output sample rate and channel count (resampling/remixing as needed).
// Positions are in output samples from the start of the media.
// Sequential reads are streamed; discontinuous reads seek.
class AudioDecoder {
public:
    static Result<std::unique_ptr<AudioDecoder>> open(const std::filesystem::path& path, int outSampleRate,
                                                      int outChannels = 2);
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    int sampleRate() const;
    int channels() const;

    // Fills `out` (frameCount * channels floats) with audio starting at output sample
    // `startSample`. Regions outside the media are filled with silence.
    Status read(int64_t startSample, int64_t frameCount, float* out);

private:
    struct Impl;
    explicit AudioDecoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace up
