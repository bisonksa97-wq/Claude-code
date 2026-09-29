#include "playback/PlaybackEngine.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include "core/Log.h"
#include "render/AudioMixer.h"
#include "render/FrameCompositor.h"

namespace up::playback {

using namespace std::chrono_literals;

void SteadyClock::restart() {
    startNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
}

double SteadyClock::elapsedSeconds() const {
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
    return static_cast<double>(now - startNs_) / 1e9;
}

PlaybackEngine::PlaybackEngine(std::shared_ptr<AudioOutput> audio, std::shared_ptr<Clock> clock)
    : audio_(std::move(audio)), clock_(std::move(clock)) {}

PlaybackEngine::~PlaybackEngine() { stop(); }

Status PlaybackEngine::start(const Project& project, const std::string& timelineId, FrameIndex from,
                             int previewWidth, int previewHeight) {
    stop();
    const Timeline* timeline = project.findTimeline(timelineId);
    if (!timeline) {
        return makeError(ErrorCode::NotFound, "playback", "The timeline to play does not exist.",
                         "Open a timeline and try again.");
    }
    if (from < 0 || from >= timeline->duration()) {
        return makeError(ErrorCode::OutOfRange, "playback", "There is nothing to play from this position.",
                         "Move the playhead onto the timeline's content.");
    }
    project_ = std::make_unique<Project>(project);
    timelineId_ = timelineId;
    rate_ = timeline->frameRate;
    sampleRate_ = timeline->sampleRate;
    from_ = from;
    end_ = timeline->duration();
    width_ = previewWidth > 0 ? previewWidth : timeline->width;
    height_ = previewHeight > 0 ? previewHeight : timeline->height;
    fifo_ = std::make_unique<SampleFifo>(kChannels, sampleRate_ / 2);  // 500 ms of mix-ahead
    mixedUntil_ = frameToSample(from_, rate_, sampleRate_);
    frames_.clear();
    lastShown_ = -1;
    dropped_ = 0;
    underruns_ = 0;
    {
        std::lock_guard lock(errorMutex_);
        error_.reset();
    }
    stopping_ = false;
    running_ = true;

    audioThread_ = std::thread(&PlaybackEngine::audioLoop, this);
    // Prime the audio buffer (up to 150 ms) so the device does not start with an underrun.
    const int64_t prime = std::min<int64_t>(sampleRate_ * 15 / 100,
                                            frameToSample(end_, rate_, sampleRate_) - mixedUntil_);
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (fifo_->size() < prime && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);

    audioActive_ = false;
    if (audio_) {
        Status s = audio_->start(sampleRate_, kChannels, [this](float* out, int64_t frames) { pull(out, frames); });
        if (s.ok()) {
            audioActive_ = true;
        } else {
            UP_LOG_WARN(log::sub::Audio, "Audio output unavailable, playing without sound: " << s.error().message);
        }
    }
    clock_->restart();
    videoThread_ = std::thread(&PlaybackEngine::videoLoop, this);
    UP_LOG_DEBUG(log::sub::Render, "Playback started at frame " << from_ << (audioActive_ ? " (audio clock)" : " (wall clock)"));
    return Status::success();
}

void PlaybackEngine::stop() {
    if (!running_ && !videoThread_.joinable() && !audioThread_.joinable()) return;
    if (audioActive_ && audio_) audio_->stop();
    audioActive_ = false;
    stopping_ = true;
    wake_.notify_all();
    if (videoThread_.joinable()) videoThread_.join();
    if (audioThread_.joinable()) audioThread_.join();
    running_ = false;
    std::lock_guard lock(framesMutex_);
    frames_.clear();
    lastShown_ = -1;
}

FrameIndex PlaybackEngine::position() const {
    if (!running_) return from_;
    const double seconds = audioActive_ ? static_cast<double>(audio_->playedFrames()) / sampleRate_
                                        : clock_->elapsedSeconds();
    return from_ + secondsToFrames(std::max(0.0, seconds), rate_);
}

std::optional<DisplayFrame> PlaybackEngine::frameForDisplay() {
    std::lock_guard lock(framesMutex_);
    const FrameIndex now = position();
    auto it = frames_.upper_bound(now);
    if (it == frames_.begin()) return std::nullopt;
    --it;
    if (it->first <= lastShown_) return std::nullopt;
    // Anything older than the frame we show now was rendered in time but never displayed.
    dropped_ += std::distance(frames_.begin(), it);
    DisplayFrame out{it->first, std::move(it->second)};
    frames_.erase(frames_.begin(), std::next(it));
    lastShown_ = out.index;
    wake_.notify_all();
    return out;
}

PlaybackStats PlaybackEngine::stats() const { return {dropped_.load(), underruns_.load()}; }

int64_t PlaybackEngine::bufferedAudioFrames() const { return fifo_ ? fifo_->size() : 0; }

Status PlaybackEngine::lastError() const {
    std::lock_guard lock(errorMutex_);
    if (error_) return *error_;
    return Status::success();
}

void PlaybackEngine::fail(const Error& error) {
    UP_LOG_ERROR(log::sub::Render, "Playback: " << error.message);
    std::lock_guard lock(errorMutex_);
    if (!error_) error_ = error;
}

void PlaybackEngine::pull(float* out, int64_t frames) {
    const int64_t got = fifo_->pop(out, frames);
    if (got < frames) {
        std::fill(out + got * kChannels, out + frames * kChannels, 0.0f);
        if (mixedUntil_ < frameToSample(end_, rate_, sampleRate_)) ++underruns_;
    }
    wake_.notify_all();
}

void PlaybackEngine::audioLoop() {
    render::AudioMixer mixer(render::resolverFor(*project_));
    const Timeline* timeline = project_->findTimeline(timelineId_);
    const int64_t endSample = frameToSample(end_, rate_, sampleRate_);
    constexpr int64_t kChunk = 1024;
    std::vector<float> buffer;
    while (!stopping_) {
        const int64_t pos = mixedUntil_;
        if (pos >= endSample || fifo_->space() < kChunk) {
            std::unique_lock lock(framesMutex_);
            wake_.wait_for(lock, 5ms, [this] { return stopping_.load(); });
            continue;
        }
        const int64_t n = std::min(kChunk, endSample - pos);
        Status s = mixer.mix(*timeline, pos, n, buffer);
        if (!s.ok()) {
            fail(s.error());
            buffer.assign(static_cast<std::size_t>(n) * kChannels, 0.0f);
        }
        fifo_->push(buffer.data(), n);
        mixedUntil_ = pos + n;
    }
}

void PlaybackEngine::videoLoop() {
    render::FrameCompositor compositor(render::resolverFor(*project_), 8);
    const Timeline* timeline = project_->findTimeline(timelineId_);
    FrameIndex next = from_;
    while (!stopping_) {
        const FrameIndex now = position();
        if (next < now) {  // fell behind the clock: skip instead of stalling
            dropped_ += now - next;
            next = now;
        }
        bool full = false;
        {
            std::lock_guard lock(framesMutex_);
            full = static_cast<int>(frames_.size()) >= kVideoFramesAhead;
        }
        if (full || next >= end_) {
            std::unique_lock lock(framesMutex_);
            wake_.wait_for(lock, 4ms, [this] { return stopping_.load(); });
            continue;
        }
        auto frame = compositor.render(*timeline, next, width_, height_);
        if (!frame.ok()) {
            fail(frame.error());
            ++next;
            continue;
        }
        {
            std::lock_guard lock(framesMutex_);
            frames_[next] = std::move(frame.value());
        }
        ++next;
    }
}

}  // namespace up::playback
