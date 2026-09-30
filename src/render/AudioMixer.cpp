#include "render/AudioMixer.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"
#include "timeline/Transitions.h"

namespace up::render {

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

AudioMixer::AudioMixer(MediaResolver resolver, std::size_t decoderCapacity)
    : resolver_(std::move(resolver)), pool_(decoderCapacity) {}

Status AudioMixer::mix(const Timeline& timeline, int64_t start, int64_t count, std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(std::max<int64_t>(count, 0)) * kChannels, 0.0f);
    if (count <= 0) return Status::success();
    const int rate = timeline.sampleRate;
    const int64_t end = start + count;

    const auto tracks = timeline.tracksOfKind(TrackKind::Audio);
    const bool anySolo = std::any_of(tracks.begin(), tracks.end(), [](const Track* t) { return t->solo; });

    const double framesPerSample = timeline.frameRate.toDouble() / rate;
    for (const Track* track : tracks) {
        if (!track->enabled || track->muted || (anySolo && !track->solo)) continue;
        const auto regions = transitions::regions(*track);
        for (const Clip& clip : track->clips) {
            if (!clip.enabled) continue;
            // Audible range: the clip body plus handles used by edit-point transitions.
            const auto [firstFrame, lastFrame] = transitions::audibleRange(regions, clip);
            const int64_t clipStart = frameToSample(clip.start, timeline.frameRate, rate);
            const int64_t from = std::max(start, frameToSample(firstFrame, timeline.frameRate, rate));
            const int64_t to = std::min(end, frameToSample(lastFrame, timeline.frameRate, rate));
            if (to <= from) continue;

            const MediaItem* media = resolver_ ? resolver_(clip.mediaId) : nullptr;
            if (!media || !media->online || !media->info.hasAudio) continue;
            auto decoder = pool_.audio(clip.id, media->path, rate, kChannels);
            if (!decoder.ok()) {
                UP_LOG_WARN(log::sub::Audio, decoder.error().message);
                continue;
            }
            const int64_t sourceStart = frameToSample(clip.sourceIn, timeline.frameRate, rate) + (from - clipStart);
            scratch_.resize(static_cast<std::size_t>(to - from) * kChannels);
            UP_TRY(decoder.value()->read(sourceStart, to - from, scratch_.data()));
            const auto gain = static_cast<float>(dbToLinear(track->gainDb + clip.gainDb));
            const bool faded = std::any_of(regions.begin(), regions.end(), [&](const transitions::Region& r) {
                return r.outgoing == &clip || r.incoming == &clip;
            });
            float* dst = out.data() + (from - start) * kChannels;
            for (int64_t i = 0; i < to - from; ++i) {
                float g = gain;
                if (faded) {
                    // Sample-accurate envelope: position of this sample in (fractional) timeline frames.
                    const double framePos = static_cast<double>(from + i) * framesPerSample;
                    g *= static_cast<float>(transitions::audioEnvelope(regions, clip, framePos));
                }
                for (int c = 0; c < kChannels; ++c) dst[i * kChannels + c] += scratch_[static_cast<std::size_t>(i * kChannels + c)] * g;
            }
        }
    }
    return Status::success();
}

}  // namespace up::render
