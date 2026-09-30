#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioEffect.h"
#include "core/Result.h"
#include "render/MediaSource.h"
#include "timeline/Timeline.h"

namespace up::render {

// Levels of one mix call (linear, 1.0 = full scale).
struct ChannelMeter {
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    float rms = 0.0f;
};

struct MixMeters {
    std::map<std::string, ChannelMeter> tracks;  // by track id, after track gain and pan
    ChannelMeter master;
};

// Mixes a timeline's audio tracks to interleaved stereo float.
//
// Per track: every enabled clip is summed into the track bus with its clip gain (dB),
// keyframable Volume (dB) and Pan, and its sample-accurate transition envelope
// (constant-power crossfades and fades; clips play into their handles during
// edit-point transitions). The bus then runs through the track's insert effects,
// then track gain and pan (balance law with unity at centre), into the master.
// Disabled or muted tracks are silent; if any track is soloed only soloed tracks
// play. Effect state carries over between consecutive calls and is reset when a
// call does not continue where the previous one ended (a seek). The output is not
// clipped here (the encoder clamps).
class AudioMixer {
public:
    static constexpr int kChannels = 2;

    explicit AudioMixer(MediaResolver resolver, std::size_t decoderCapacity = 16);
    ~AudioMixer();

    // Mixes `count` samples starting at timeline sample `start` (timeline.sampleRate).
    Status mix(const Timeline& timeline, int64_t start, int64_t count, std::vector<float>& out,
               MixMeters* meters = nullptr);

private:
    struct TrackState;
    Status mixClips(const Timeline& timeline, const Track& track, int64_t start, int64_t count);
    TrackState& prepareEffects(const Track& track, int sampleRate, int64_t start);

    MediaResolver resolver_;
    DecoderPool pool_;
    std::vector<float> scratch_;
    std::vector<float> bus_;
    std::map<std::string, std::unique_ptr<TrackState>> tracks_;
};

double dbToLinear(double db);
ChannelMeter measure(const float* interleaved, int64_t frames);

}  // namespace up::render
