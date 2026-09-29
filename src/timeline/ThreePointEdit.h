#pragma once

#include <optional>

#include "core/Rational.h"
#include "core/Result.h"

namespace up::ops {

// Inputs to a three-point edit. All values are frames at the timeline rate;
// out marks are exclusive.
struct ThreePointInput {
    std::optional<FrameIndex> sourceIn;
    std::optional<FrameIndex> sourceOut;
    std::optional<FrameIndex> recordIn;   // timeline mark in
    std::optional<FrameIndex> recordOut;  // timeline mark out
    FrameIndex playhead = 0;
    FrameIndex sourceLength = 0;          // 0 = unbounded (stills)
    FrameIndex defaultDuration = 125;     // used for unbounded sources without marks
};

struct ThreePointResult {
    FrameIndex sourceIn = 0;
    FrameIndex recordIn = 0;
    FrameIndex duration = 0;
    FrameIndex recordOut() const { return recordIn + duration; }
};

// Resolves source and record ranges from whichever marks are set:
//  - Duration: the timeline range when both record marks are set (a four-point
//    edit keeps the source in and fits the timeline range); otherwise the source
//    range; otherwise from source in (or 0) to the end of the media.
//  - Source in: the source in mark; else backtimed from the source out; else 0.
//  - Record in: the record in mark; else backtimed from the record out; else the playhead.
// Fails with a readable error when the result would leave the media or start before 0.
Result<ThreePointResult> resolveThreePointEdit(const ThreePointInput& in);

}  // namespace up::ops
