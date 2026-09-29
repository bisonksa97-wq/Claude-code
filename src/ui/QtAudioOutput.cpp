#include "ui/QtAudioOutput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <algorithm>
#include <cstring>
#include <vector>

#include "core/Log.h"

namespace up::ui {

// QIODevice the sink reads from; every read pulls fresh samples from the engine.
class PullDevice final : public QIODevice {
public:
    PullDevice(playback::AudioOutput::Pull pull, int channels, bool float32, std::atomic<int64_t>& pulled,
               QObject* parent)
        : QIODevice(parent), pull_(std::move(pull)), channels_(channels), float32_(float32), pulled_(pulled) {}

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 20; }  // an endless stream

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const qint64 bytesPerFrame = channels_ * (float32_ ? 4 : 2);
        const qint64 frames = maxlen / bytesPerFrame;
        if (frames <= 0) return 0;
        scratch_.resize(static_cast<std::size_t>(frames * channels_));
        pull_(scratch_.data(), frames);
        if (float32_) {
            std::memcpy(data, scratch_.data(), static_cast<std::size_t>(frames * bytesPerFrame));
        } else {
            for (std::size_t i = 0; i < scratch_.size(); ++i) {
                const auto v = static_cast<int16_t>(std::clamp(scratch_[i], -1.0f, 1.0f) * 32767.0f);
                std::memcpy(data + i * 2, &v, 2);
            }
        }
        pulled_ += frames;
        return frames * bytesPerFrame;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    playback::AudioOutput::Pull pull_;
    int channels_;
    bool float32_;
    std::atomic<int64_t>& pulled_;
    std::vector<float> scratch_;
};

QtAudioOutput::QtAudioOutput(QObject* parent) : QObject(parent) {}

QtAudioOutput::~QtAudioOutput() { stop(); }

bool QtAudioOutput::deviceAvailable() { return !QMediaDevices::defaultAudioOutput().isNull(); }

Status QtAudioOutput::start(int sampleRate, int channels, Pull pull) {
    stop();
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        return makeError(ErrorCode::NotFound, "audio", "No audio output device was found.",
                         "Connect or enable an audio output; playback continues without sound.");
    }
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Float);
    bool float32 = true;
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        float32 = false;
        if (!device.isFormatSupported(format)) {
            return makeError(ErrorCode::NotFound, "audio",
                             "The audio device '" + device.description().toStdString() + "' does not support " +
                                 std::to_string(sampleRate) + " Hz stereo output.",
                             "Choose a different output device or timeline sample rate.");
        }
    }
    pulled_ = 0;
    const int bytesPerFrame = channels * (float32 ? 4 : 2);
    sink_ = new QAudioSink(device, format, this);
    sink_->setBufferSize(sampleRate / 12 * bytesPerFrame);  // ~80 ms keeps A/V latency low
    device_ = new PullDevice(std::move(pull), channels, float32, pulled_, this);
    device_->open(QIODevice::ReadOnly);
    sink_->start(device_);
    if (sink_->error() != QAudio::NoError) {
        stop();
        return makeError(ErrorCode::IoError, "audio", "The audio device could not be started.",
                         "Check the system's audio settings; playback continues without sound.");
    }
    bufferedFrames_ = sink_->bufferSize() / bytesPerFrame;
    UP_LOG_INFO(log::sub::Audio, "Audio output: " << device.description().toStdString() << ", " << sampleRate << " Hz, "
                                                  << (float32 ? "float" : "int16") << ", buffer " << bufferedFrames_
                                                  << " frames");
    return Status::success();
}

void QtAudioOutput::stop() {
    if (sink_) {
        sink_->stop();
        delete sink_;
        sink_ = nullptr;
    }
    if (device_) {
        device_->close();
        delete device_;
        device_ = nullptr;
    }
}

int64_t QtAudioOutput::playedFrames() const { return std::max<int64_t>(0, pulled_ - bufferedFrames_); }

}  // namespace up::ui
