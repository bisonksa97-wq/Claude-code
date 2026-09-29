#pragma once

#include <QObject>
#include <atomic>
#include <memory>

#include "playback/AudioOutput.h"

class QAudioSink;

namespace up::ui {

class PullDevice;

// Plays PlaybackEngine audio on the system's default output through Qt Multimedia.
// Float samples are used when the device supports them, otherwise 16-bit PCM.
// The played position is estimated as frames pulled minus the sink's buffer.
class QtAudioOutput final : public QObject, public playback::AudioOutput {
    Q_OBJECT
public:
    explicit QtAudioOutput(QObject* parent = nullptr);
    ~QtAudioOutput() override;

    Status start(int sampleRate, int channels, Pull pull) override;
    void stop() override;
    int64_t playedFrames() const override;

    // Whether a default audio output device currently exists.
    static bool deviceAvailable();

private:
    QAudioSink* sink_ = nullptr;
    PullDevice* device_ = nullptr;
    std::atomic<int64_t> pulled_{0};
    int64_t bufferedFrames_ = 0;
};

}  // namespace up::ui
