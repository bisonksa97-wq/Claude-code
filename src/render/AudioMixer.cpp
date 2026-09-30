#include "render/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "core/Log.h"
#include "timeline/Transitions.h"

namespace up::render {

double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

ChannelMeter measure(const float* data, int64_t frames) {
    ChannelMeter m;
    double sum = 0.0;
    for (int64_t i = 0; i < frames; ++i) {
        const float l = data[i * 2];
        const float r = data[i * 2 + 1];
        m.peakLeft = std::max(m.peakLeft, std::abs(l));
        m.peakRight = std::max(m.peakRight, std::abs(r));
        sum += static_cast<double>(l) * l + static_cast<double>(r) * r;
    }
    if (frames > 0) m.rms = static_cast<float>(std::sqrt(sum / static_cast<double>(frames * 2)));
    return m;
}

struct AudioMixer::TrackState {
    std::string signature;  // effect chain + sample rate the processors were built for
    std::vector<std::unique_ptr<audio::AudioProcessor>> chain;
    int64_t nextSample = -1;  // where the previous call on this track ended
};

AudioMixer::AudioMixer(MediaResolver resolver, std::size_t decoderCapacity)
    : resolver_(std::move(resolver)), pool_(decoderCapacity) {}

AudioMixer::~AudioMixer() = default;

AudioMixer::TrackState& AudioMixer::prepareEffects(const Track& track, int sampleRate, int64_t start) {
    auto& slot = tracks_[track.id];
    if (!slot) slot = std::make_unique<TrackState>();
    TrackState& state = *slot;
    std::ostringstream sig;
    sig.precision(17);
    sig << sampleRate;
    for (const auto& fx : track.effects) {
        if (!fx.enabled) continue;
        sig << '|' << fx.id << ':' << fx.type;
        for (const auto& [k, v] : fx.params) sig << ',' << k << '=' << v;
    }
    if (sig.str() != state.signature) {
        // The chain changed: rebuild the processors (parameters are fixed per instance).
        state.chain.clear();
        for (const auto& fx : track.effects) {
            if (!fx.enabled) continue;
            auto p = audio::createProcessor(fx);
            if (!p.ok()) {
                UP_LOG_WARN(log::sub::Audio, p.error().message);
                continue;
            }
            p.value()->prepare(sampleRate, kChannels);
            state.chain.push_back(std::move(p.value()));
        }
        state.signature = sig.str();
    } else if (state.nextSample != start) {
        for (auto& p : state.chain) p->reset();  // discontinuity: do not smear state across a seek
    }
    return state;
}

Status AudioMixer::mixClips(const Timeline& timeline, const Track& track, int64_t start, int64_t count) {
    const int rate = timeline.sampleRate;
    const int64_t end = start + count;
    const double framesPerSample = timeline.frameRate.toDouble() / rate;
    const auto regions = transitions::regions(track);
    for (const Clip& clip : track.clips) {
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

        const AnimatedValue& volume = clip.transform[ClipParam::Volume];
        const AnimatedValue& pan = clip.transform[ClipParam::Pan];
        const bool faded = std::any_of(regions.begin(), regions.end(), [&](const transitions::Region& r) {
            return r.outgoing == &clip || r.incoming == &clip;
        });
        const bool automated = faded || volume.animated() || pan.animated();
        const double clipGain = dbToLinear(clip.gainDb);
        auto gainsAt = [&](double framePos) {
            const double sourceFrame = framePos - static_cast<double>(clip.start) + static_cast<double>(clip.sourceIn);
            double g = clipGain * dbToLinear(std::clamp(volume.atFractional(sourceFrame), -60.0, 12.0));
            if (faded) g *= transitions::audioEnvelope(regions, clip, framePos);
            const auto [l, r] = audio::panGains(std::clamp(pan.atFractional(sourceFrame), -100.0, 100.0) / 100.0);
            return std::pair<float, float>{static_cast<float>(g * l), static_cast<float>(g * r)};
        };
        auto [gl, gr] = gainsAt(static_cast<double>(from) * framesPerSample);
        float* dst = bus_.data() + (from - start) * kChannels;
        for (int64_t i = 0; i < to - from; ++i) {
            if (automated) std::tie(gl, gr) = gainsAt(static_cast<double>(from + i) * framesPerSample);
            dst[i * 2] += scratch_[static_cast<std::size_t>(i * 2)] * gl;
            dst[i * 2 + 1] += scratch_[static_cast<std::size_t>(i * 2 + 1)] * gr;
        }
    }
    return Status::success();
}

Status AudioMixer::mix(const Timeline& timeline, int64_t start, int64_t count, std::vector<float>& out, MixMeters* meters) {
    out.assign(static_cast<std::size_t>(std::max<int64_t>(count, 0)) * kChannels, 0.0f);
    if (meters) *meters = MixMeters{};
    if (count <= 0) return Status::success();

    const auto tracks = timeline.tracksOfKind(TrackKind::Audio);
    const bool anySolo = std::any_of(tracks.begin(), tracks.end(), [](const Track* t) { return t->solo; });
    for (const Track* track : tracks) {
        if (!track->enabled || track->muted || (anySolo && !track->solo)) {
            if (auto it = tracks_.find(track->id); it != tracks_.end()) it->second->nextSample = -1;
            if (meters) meters->tracks[track->id] = ChannelMeter{};
            continue;
        }
        bus_.assign(out.size(), 0.0f);
        UP_TRY(mixClips(timeline, *track, start, count));
        TrackState& state = prepareEffects(*track, timeline.sampleRate, start);
        for (auto& p : state.chain) p->process(bus_.data(), count);
        state.nextSample = start + count;

        const auto [pl, pr] = audio::panGains(track->pan);
        const auto g = static_cast<float>(dbToLinear(track->gainDb));
        for (int64_t i = 0; i < count; ++i) {
            bus_[static_cast<std::size_t>(i * 2)] *= g * pl;
            bus_[static_cast<std::size_t>(i * 2 + 1)] *= g * pr;
        }
        if (meters) meters->tracks[track->id] = measure(bus_.data(), count);
        for (std::size_t i = 0; i < out.size(); ++i) out[i] += bus_[i];
    }
    if (meters) meters->master = measure(out.data(), count);
    return Status::success();
}

}  // namespace up::render
