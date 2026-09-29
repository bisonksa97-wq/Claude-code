#include "media/SyntheticMedia.h"

#include <cmath>
#include <vector>

namespace up::media {
namespace {

constexpr double kPi = 3.14159265358979323846;

void drawFrame(VideoFrame& f, const SyntheticSpec& spec, FrameIndex index) {
    switch (spec.pattern) {
        case SyntheticSpec::Pattern::Solid:
            f.fill(spec.color[0], spec.color[1], spec.color[2]);
            break;
        case SyntheticSpec::Pattern::FrameRamp: {
            const auto v = static_cast<uint8_t>((index * spec.rampStep) % 256);
            f.fill(v, v, v);
            break;
        }
        case SyntheticSpec::Pattern::Bars: {
            static constexpr uint8_t bars[7][3] = {{192, 192, 192}, {192, 192, 0}, {0, 192, 192}, {0, 192, 0},
                                                   {192, 0, 192},   {192, 0, 0},   {0, 0, 192}};
            const int marker = static_cast<int>((index * 4) % std::max(1, f.width));
            for (int y = 0; y < f.height; ++y) {
                uint8_t* row = f.row(y);
                for (int x = 0; x < f.width; ++x) {
                    const int bar = std::min(6, x * 7 / f.width);
                    const bool isMarker = y > f.height * 3 / 4 && std::abs(x - marker) < 4;
                    row[x * 4 + 0] = isMarker ? 255 : bars[bar][0];
                    row[x * 4 + 1] = isMarker ? 255 : bars[bar][1];
                    row[x * 4 + 2] = isMarker ? 255 : bars[bar][2];
                    row[x * 4 + 3] = 255;
                }
            }
            break;
        }
    }
}

}  // namespace

Status generateSyntheticMedia(const std::filesystem::path& path, const SyntheticSpec& spec) {
    if (spec.frames <= 0) {
        return makeError(ErrorCode::InvalidArgument, "media", "Generated media must have at least one frame.");
    }
    EncodeSettings settings;
    settings.video = spec.video;
    settings.width = spec.width;
    settings.height = spec.height;
    settings.frameRate = spec.frameRate;
    settings.videoCodec = spec.videoCodec;
    settings.crf = 12;
    settings.audio = spec.audio;
    settings.sampleRate = spec.sampleRate;
    settings.channels = 2;
    auto writer = MediaWriter::open(path, settings);
    if (!writer.ok()) return writer.error();

    VideoFrame frame(spec.width, spec.height);
    std::vector<float> audio;
    for (FrameIndex i = 0; i < spec.frames; ++i) {
        if (spec.video) {
            drawFrame(frame, spec, i);
            UP_TRY(writer.value()->writeVideo(frame));
        }
        if (spec.audio) {
            const int64_t s0 = frameToSample(i, spec.frameRate, spec.sampleRate);
            const int64_t s1 = frameToSample(i + 1, spec.frameRate, spec.sampleRate);
            audio.assign(static_cast<std::size_t>(s1 - s0) * 2, 0.0f);
            for (int64_t s = s0; s < s1; ++s) {
                const float v = spec.toneHz > 0
                                    ? spec.toneLevel * static_cast<float>(std::sin(2.0 * kPi * spec.toneHz *
                                                                                   static_cast<double>(s) / spec.sampleRate))
                                    : 0.0f;
                audio[static_cast<std::size_t>(s - s0) * 2] = v;
                audio[static_cast<std::size_t>(s - s0) * 2 + 1] = v;
            }
            UP_TRY(writer.value()->writeAudio(audio.data(), s1 - s0));
        }
    }
    return writer.value()->finish();
}

}  // namespace up::media
