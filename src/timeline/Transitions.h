#pragma once

#include <utility>
#include <vector>

#include "timeline/Timeline.h"

// Evaluation of transitions and fades on a track.
//
// Transitions are stored on clips (Clip::transitionIn / transitionOut); this module
// turns them into concrete time regions. Regions are clamped to what the media
// allows (handles) and to the clips' lengths, so an edit that shortens a clip can
// never make a timeline invalid: its transition just gets shorter (possibly zero).
namespace up::transitions {

struct Region {
    const Clip* outgoing = nullptr;  // null: fade in from below/silence
    const Clip* incoming = nullptr;  // null: fade out to below/silence
    FrameIndex start = 0;            // timeline frames [start, end)
    FrameIndex end = 0;
    TransitionKind kind = TransitionKind::Dissolve;
    FrameIndex requested = 0;        // duration asked for (end - start may be shorter)

    FrameIndex length() const { return end - start; }
    // Position through the region at a (possibly fractional) timeline frame, 0..1.
    double progress(double frame) const;
};

// All effective regions on a track, sorted by start, non-overlapping, non-empty.
std::vector<Region> regions(const Track& track);

// What a track shows at one frame.
struct TrackFrame {
    const Clip* clip = nullptr;  // the single visible clip when not in a transition
    const Region* region = nullptr;  // set during a transition or fade (points into `regions`)
    double progress = 0.0;
};
TrackFrame evaluate(const Track& track, const std::vector<Region>& regions, FrameIndex frame);

// Picture mix: result = withOutgoing * first + below * second + withIncoming * third,
// where "withX" is the canvas after compositing X over `below`.
struct VideoWeights {
    double outgoing = 0.0;
    double below = 0.0;
    double incoming = 0.0;
};
VideoWeights videoWeights(TransitionKind kind, double progress);

// Constant-power gains (outgoing, incoming) for audio at `progress`.
std::pair<double, double> audioGains(TransitionKind kind, double progress);

// Gain envelope (0..1) of `clip` at a fractional timeline frame, and the frame range in
// which the clip is audible (its body extended by edit-point transitions into handles).
double audioEnvelope(const std::vector<Region>& regions, const Clip& clip, double frame);
std::pair<FrameIndex, FrameIndex> audibleRange(const std::vector<Region>& regions, const Clip& clip);

// Longest duration a transition at `edge` of `clip` could have (0 if none fits).
FrameIndex maxDuration(const Track& track, const Clip& clip, bool atHead, TransitionAlignment alignment);

}  // namespace up::transitions
