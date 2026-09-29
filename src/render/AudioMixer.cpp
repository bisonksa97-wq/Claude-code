#include "render/AudioMixer.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"

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

    for (const Track* track : tracks) {
        if (!track->enabled || track->muted || (anySolo && !track->solo)) continue;
        for (const Clip& clip : track->clips) {
            if (!clip.enabled) continue;
            const int64_t clipStart = frameToSample(clip.start, timeline.frameRate, rate);
            const int64_t clipEnd = frameToSample(clip.end(), timeline.frameRate, rate);
            const int64_t from = std::max(start, clipStart);
            const int64_t to = std::min(end, clipEnd);
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
            float* dst = out.data() + (from - start) * kChannels;
            for (std::size_t i = 0; i < scratch_.size(); ++i) dst[i] += scratch_[i] * gain;
        }
    }
    return Status::success();
}

}  // namespace up::render
