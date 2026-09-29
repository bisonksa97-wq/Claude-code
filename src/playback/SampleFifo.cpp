#include "playback/SampleFifo.h"

#include <algorithm>
#include <cstring>

namespace up::playback {

SampleFifo::SampleFifo(int channels, int64_t capacityFrames)
    : channels_(channels), capacity_(capacityFrames), buffer_(static_cast<std::size_t>(capacityFrames * channels)) {}

int64_t SampleFifo::push(const float* data, int64_t frames) {
    std::lock_guard lock(mutex_);
    const int64_t n = std::min(frames, capacity_ - count_);
    int64_t writePos = (readPos_ + count_) % capacity_;
    int64_t done = 0;
    while (done < n) {
        const int64_t chunk = std::min(n - done, capacity_ - writePos);
        std::memcpy(buffer_.data() + writePos * channels_, data + done * channels_,
                    static_cast<std::size_t>(chunk * channels_) * sizeof(float));
        done += chunk;
        writePos = (writePos + chunk) % capacity_;
    }
    count_ += n;
    return n;
}

int64_t SampleFifo::pop(float* out, int64_t frames) {
    std::lock_guard lock(mutex_);
    const int64_t n = std::min(frames, count_);
    int64_t done = 0;
    while (done < n) {
        const int64_t chunk = std::min(n - done, capacity_ - readPos_);
        std::memcpy(out + done * channels_, buffer_.data() + readPos_ * channels_,
                    static_cast<std::size_t>(chunk * channels_) * sizeof(float));
        done += chunk;
        readPos_ = (readPos_ + chunk) % capacity_;
    }
    count_ -= n;
    return n;
}

int64_t SampleFifo::size() const {
    std::lock_guard lock(mutex_);
    return count_;
}

int64_t SampleFifo::space() const {
    std::lock_guard lock(mutex_);
    return capacity_ - count_;
}

void SampleFifo::clear() {
    std::lock_guard lock(mutex_);
    readPos_ = 0;
    count_ = 0;
}

}  // namespace up::playback
