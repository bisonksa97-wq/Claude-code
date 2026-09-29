#pragma once

#include <cstdint>
#include <vector>

#include "core/Result.h"
#include "render/MediaSource.h"
#include "timeline/Timeline.h"

namespace up::render {

// Mixes a timeline's audio tracks to interleaved stereo float.
//
// Track rules: disabled or muted tracks are silent; if any track is soloed only
// soloed tracks play. Gain = track gain (dB) + clip gain (dB). No panning,
// effects or automation yet. Output is not clipped here (the encoder clamps).
class AudioMixer {
public:
    static constexpr int kChannels = 2;

    explicit AudioMixer(MediaResolver resolver, std::size_t decoderCapacity = 16);

    // Mixes `count` samples starting at timeline sample `start` (timeline.sampleRate).
    Status mix(const Timeline& timeline, int64_t start, int64_t count, std::vector<float>& out);

private:
    MediaResolver resolver_;
    DecoderPool pool_;
    std::vector<float> scratch_;
};

double dbToLinear(double db);

}  // namespace up::render
