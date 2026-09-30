#include "timeline/Transitions.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace up::transitions {
namespace {

constexpr double kHalfPi = 1.57079632679489661923;
constexpr FrameIndex kUnbounded = std::numeric_limits<FrameIndex>::max() / 4;

// Frames of media available after the clip's out point (handle), unbounded for stills.
FrameIndex tailHandle(const Clip& c) { return c.bounded() ? c.sourceLength - c.sourceOut() : kUnbounded; }
FrameIndex headHandle(const Clip& c) { return c.sourceIn; }

const Clip* previousAdjacent(const Track& track, std::size_t i) {
    return i > 0 && track.clips[i - 1].end() == track.clips[i].start ? &track.clips[i - 1] : nullptr;
}

const Clip* nextAdjacent(const Track& track, std::size_t i) {
    return i + 1 < track.clips.size() && track.clips[i + 1].start == track.clips[i].end() ? &track.clips[i + 1] : nullptr;
}

// Frames of an edit-point transition before and after the cut.
std::pair<FrameIndex, FrameIndex> split(FrameIndex duration, TransitionAlignment alignment) {
    switch (alignment) {
        case TransitionAlignment::StartAtCut: return {0, duration};
        case TransitionAlignment::EndAtCut: return {duration, 0};
        case TransitionAlignment::Center:
        default: return {duration / 2, duration - duration / 2};
    }
}

}  // namespace

double Region::progress(double frame) const {
    if (length() <= 0) return 0.0;
    // Sample the middle of each frame so the first and last frames are both mixed.
    return std::clamp((frame - static_cast<double>(start) + 0.5) / static_cast<double>(length()), 0.0, 1.0);
}

std::vector<Region> regions(const Track& track) {
    std::vector<Region> out;
    // Frames at the head of each clip already used by its incoming transition or fade.
    std::vector<FrameIndex> usedHead(track.clips.size(), 0);

    for (std::size_t i = 0; i < track.clips.size(); ++i) {
        const Clip& b = track.clips[i];
        if (!b.transitionIn) continue;
        const Transition& t = *b.transitionIn;
        if (const Clip* a = previousAdjacent(track, i)) {
            auto [before, after] = split(t.duration, t.alignment);
            // Before the cut, the incoming clip plays from its head handle; after it, the
            // outgoing clip plays from its tail handle. Neither may exceed the clips themselves.
            before = std::min({before, headHandle(b), a->duration});
            after = std::min({after, tailHandle(*a), b.duration});
            if (before + after <= 0) continue;
            out.push_back({a, &b, b.start - before, b.start + after, t.kind, t.duration});
            usedHead[i] = after;
        } else {
            const FrameIndex d = std::min(t.duration, b.duration);
            out.push_back({nullptr, &b, b.start, b.start + d, t.kind, t.duration});
            usedHead[i] = d;
        }
    }
    for (std::size_t i = 0; i < track.clips.size(); ++i) {
        const Clip& a = track.clips[i];
        if (!a.transitionOut) continue;
        // A tail fade gives way to an edit-point transition owned by the next clip.
        const Clip* next = nextAdjacent(track, i);
        if (next && next->transitionIn) continue;
        const FrameIndex d = std::min(a.transitionOut->duration, a.duration - usedHead[i]);
        if (d <= 0) continue;
        out.push_back({&a, nullptr, a.end() - d, a.end(), a.transitionOut->kind, a.transitionOut->duration});
    }
    std::sort(out.begin(), out.end(), [](const Region& x, const Region& y) { return x.start < y.start; });
    // Resolve any remaining overlap (very short clips) by trimming the later region.
    for (std::size_t i = 1; i < out.size(); ++i) out[i].start = std::max(out[i].start, out[i - 1].end);
    out.erase(std::remove_if(out.begin(), out.end(), [](const Region& r) { return r.length() <= 0; }), out.end());
    return out;
}

TrackFrame evaluate(const Track& track, const std::vector<Region>& regs, FrameIndex frame) {
    TrackFrame out;
    for (const auto& r : regs) {
        if (frame >= r.start && frame < r.end) {
            out.region = &r;
            out.progress = r.progress(static_cast<double>(frame));
            return out;
        }
    }
    out.clip = track.clipAt(frame);
    return out;
}

VideoWeights videoWeights(TransitionKind kind, double p) {
    p = std::clamp(p, 0.0, 1.0);
    if (kind == TransitionKind::Dip) {
        if (p < 0.5) return {1.0 - 2.0 * p, 2.0 * p, 0.0};
        return {0.0, 2.0 - 2.0 * p, 2.0 * p - 1.0};
    }
    return {1.0 - p, 0.0, p};
}

std::pair<double, double> audioGains(TransitionKind kind, double p) {
    p = std::clamp(p, 0.0, 1.0);
    if (kind == TransitionKind::Dip) {
        if (p < 0.5) return {std::cos(2.0 * p * kHalfPi), 0.0};
        return {0.0, std::sin((2.0 * p - 1.0) * kHalfPi)};
    }
    return {std::cos(p * kHalfPi), std::sin(p * kHalfPi)};
}

std::pair<FrameIndex, FrameIndex> audibleRange(const std::vector<Region>& regs, const Clip& clip) {
    FrameIndex from = clip.start;
    FrameIndex to = clip.end();
    for (const auto& r : regs) {
        if (r.incoming == &clip) from = std::min(from, r.start);
        if (r.outgoing == &clip) to = std::max(to, r.end);
    }
    return {from, to};
}

double audioEnvelope(const std::vector<Region>& regs, const Clip& clip, double frame) {
    for (const auto& r : regs) {
        if (frame < static_cast<double>(r.start) || frame >= static_cast<double>(r.end)) continue;
        if (r.outgoing != &clip && r.incoming != &clip) continue;
        const auto [gOut, gIn] = audioGains(r.kind, r.progress(frame - 0.5));
        return r.outgoing == &clip ? gOut : gIn;
    }
    const auto [from, to] = audibleRange(regs, clip);
    return frame >= static_cast<double>(from) && frame < static_cast<double>(to) ? 1.0 : 0.0;
}

FrameIndex maxDuration(const Track& track, const Clip& clip, bool atHead, TransitionAlignment alignment) {
    const auto it = std::find_if(track.clips.begin(), track.clips.end(), [&](const Clip& c) { return c.id == clip.id; });
    if (it == track.clips.end()) return 0;
    const std::size_t i = static_cast<std::size_t>(it - track.clips.begin());
    if (!atHead) return clip.duration;
    const Clip* a = previousAdjacent(track, i);
    if (!a) return clip.duration;
    const FrameIndex beforeLimit = std::min(headHandle(clip), a->duration);
    const FrameIndex afterLimit = std::min(tailHandle(*a), clip.duration);
    switch (alignment) {
        case TransitionAlignment::StartAtCut: return afterLimit;
        case TransitionAlignment::EndAtCut: return beforeLimit;
        case TransitionAlignment::Center:
        default:
            // before = d/2 (floor), after = d - before (ceil)
            return std::min(2 * beforeLimit + 1, 2 * afterLimit);
    }
}

}  // namespace up::transitions
