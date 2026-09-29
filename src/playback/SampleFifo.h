#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace up::playback {

// Bounded, thread-safe FIFO of interleaved float sample frames (one producer, one consumer).
class SampleFifo {
public:
    SampleFifo(int channels, int64_t capacityFrames);

    // Returns the number of frames actually written/read.
    int64_t push(const float* data, int64_t frames);
    int64_t pop(float* out, int64_t frames);

    int64_t size() const;
    int64_t space() const;
    int64_t capacity() const { return capacity_; }
    void clear();

private:
    int channels_;
    int64_t capacity_;
    std::vector<float> buffer_;
    int64_t readPos_ = 0;  // in frames
    int64_t count_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace up::playback
