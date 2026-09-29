#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "codec/VideoFrame.h"
#include "core/Result.h"
#include "playback/AudioOutput.h"
#include "playback/SampleFifo.h"
#include "project/Project.h"

namespace up::playback {

struct PlaybackStats {
    int64_t droppedFrames = 0;   // video frames that were late and never shown
    int64_t audioUnderruns = 0;  // device pulls that found the audio buffer empty
};

struct DisplayFrame {
    FrameIndex index = 0;
    VideoFrame image;
};

// Real-time playback of a timeline snapshot.
//
//  - Master clock: the audio device's played-sample count when an AudioOutput is
//    available, otherwise a wall clock. Video follows the master clock.
//  - An audio worker mixes ahead into a FIFO the device pulls from.
//  - A video worker renders frames ahead at preview size; frames that fall behind
//    the clock are skipped (counted as dropped) instead of slowing playback.
//
// The engine owns a copy of the project, so the model can be edited while it
// plays; callers restart playback to pick up edits. Public methods are meant to
// be called from one (UI) thread.
class PlaybackEngine {
public:
    explicit PlaybackEngine(std::shared_ptr<AudioOutput> audio = nullptr,
                            std::shared_ptr<Clock> clock = std::make_shared<SteadyClock>());
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    Status start(const Project& project, const std::string& timelineId, FrameIndex from, int previewWidth,
                 int previewHeight);
    void stop();

    bool isRunning() const { return running_; }
    bool usingAudioClock() const { return audioActive_; }
    // Frame at the master clock (may exceed endFrame() once playback has finished).
    FrameIndex position() const;
    FrameIndex endFrame() const { return end_; }
    bool finished() const { return running_ && position() >= end_; }

    // The newest rendered frame at or before position(), if one newer than the last
    // returned frame is ready. Older pending frames are discarded.
    std::optional<DisplayFrame> frameForDisplay();

    PlaybackStats stats() const;
    // Frames of audio mixed ahead and waiting for the device (diagnostics/tests).
    int64_t bufferedAudioFrames() const;
    Status lastError() const;

    static constexpr int kChannels = 2;
    static constexpr int kVideoFramesAhead = 6;

private:
    void videoLoop();
    void audioLoop();
    void pull(float* out, int64_t frames);
    void fail(const Error& error);

    std::shared_ptr<AudioOutput> audio_;
    std::shared_ptr<Clock> clock_;

    std::unique_ptr<Project> project_;
    std::string timelineId_;
    FrameRate rate_{25, 1};
    int sampleRate_ = 48000;
    FrameIndex from_ = 0;
    FrameIndex end_ = 0;
    int width_ = 0;
    int height_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> audioActive_{false};

    std::unique_ptr<SampleFifo> fifo_;
    std::atomic<int64_t> mixedUntil_{0};  // timeline sample the audio worker has mixed up to

    mutable std::mutex framesMutex_;
    std::condition_variable wake_;
    std::map<FrameIndex, VideoFrame> frames_;
    FrameIndex lastShown_ = -1;

    std::atomic<int64_t> dropped_{0};
    std::atomic<int64_t> underruns_{0};
    mutable std::mutex errorMutex_;
    std::optional<Error> error_;

    std::thread videoThread_;
    std::thread audioThread_;
};

}  // namespace up::playback
