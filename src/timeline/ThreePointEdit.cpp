#include "timeline/ThreePointEdit.h"

namespace up::ops {

Result<ThreePointResult> resolveThreePointEdit(const ThreePointInput& in) {
    const bool bounded = in.sourceLength > 0;
    ThreePointResult r;

    if (in.recordIn && in.recordOut) {
        r.duration = *in.recordOut - *in.recordIn;
    } else if (in.sourceIn && in.sourceOut) {
        r.duration = *in.sourceOut - *in.sourceIn;
    } else if (in.sourceOut) {
        r.duration = *in.sourceOut;  // from the start of the media to the out mark
    } else {
        const FrameIndex from = in.sourceIn.value_or(0);
        r.duration = bounded ? in.sourceLength - from : in.defaultDuration;
    }
    if (r.duration <= 0) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "The marked range is empty.",
                         "Set an out mark after the in mark.");
    }

    if (in.sourceIn) r.sourceIn = *in.sourceIn;
    else if (in.sourceOut) r.sourceIn = *in.sourceOut - r.duration;
    else r.sourceIn = 0;

    if (in.recordIn) r.recordIn = *in.recordIn;
    else if (in.recordOut) r.recordIn = *in.recordOut - r.duration;
    else r.recordIn = in.playhead;

    if (r.sourceIn < 0) {
        return makeError(ErrorCode::OutOfRange, "timeline",
                         "There is not enough media before the source out mark to fill the edit.",
                         "Move the source out mark later or shorten the timeline range.");
    }
    if (bounded && r.sourceIn + r.duration > in.sourceLength) {
        return makeError(ErrorCode::OutOfRange, "timeline",
                         "The edit needs more media than the source has after its in mark.",
                         "Move the source in mark earlier or shorten the timeline range.");
    }
    if (r.recordIn < 0) {
        return makeError(ErrorCode::OutOfRange, "timeline", "The edit would start before the beginning of the timeline.",
                         "Move the timeline out mark later.");
    }
    return r;
}

}  // namespace up::ops
