#pragma once

#include <cstdint>
#include <functional>

#include "core/Result.h"

namespace up::playback {

// An audio device that pulls interleaved float samples from a source.
// Implementations may call `Pull` from any thread (typically the audio thread).
class AudioOutput {
public:
    // Must fill exactly `frames * channels` floats.
    using Pull = std::function<void(float* out, int64_t frames)>;

    virtual ~AudioOutput() = default;
    virtual Status start(int sampleRate, int channels, Pull pull) = 0;
    virtual void stop() = 0;
    // Sample frames that have been heard since start (i.e. pulled minus what is
    // still buffered in the device). Must be safe to call from any thread.
    virtual int64_t playedFrames() const = 0;
};

// A source of elapsed time used as the master clock when there is no audio device.
class Clock {
public:
    virtual ~Clock() = default;
    virtual void restart() = 0;
    virtual double elapsedSeconds() const = 0;
};

// Monotonic wall clock.
class SteadyClock final : public Clock {
public:
    void restart() override;
    double elapsedSeconds() const override;

private:
    int64_t startNs_ = 0;
};

}  // namespace up::playback
