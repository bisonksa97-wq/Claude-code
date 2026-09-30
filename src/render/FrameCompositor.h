#pragma once

#include "codec/VideoFrame.h"
#include <set>
#include <vector>

#include "core/Result.h"
#include "render/FloatFrame.h"
#include "render/Lut.h"
#include "render/MediaSource.h"
#include "timeline/Timeline.h"

namespace up::render {

namespace detail {
struct Layer;
}

// Produces the picture of a timeline at a given frame.
//
// Every enabled video track is composited bottom (V1) to top over black. A clip is
// fitted to the timeline frame (aspect preserved), then its transform, evaluated at
// the frame, scales, rotates, moves and crops it, and it is blended with its opacity
// (straight-alpha "over"). During a transition a track shows a mix of "below +
// outgoing", "below" and "below + incoming" (see timeline/Transitions.h), which is
// exact even over lower tracks. Layers under a fully opaque, frame-covering layer are
// skipped. Offline media renders as a layer of the offline colour so problems are
// visible rather than silently black.
//
// Colour: each decoded source is converted from its media colour space into the
// timeline colour space (float), graded there (unless bypassed on the clip or the
// timeline) and composited in float. The finished picture is converted to the output
// colour space, the output LUT is applied, and only then is it quantised to 8 bits. A LUT file that cannot be loaded is skipped with a logged warning
// (see checkTimelineLuts, which export uses to refuse such timelines).
class FrameCompositor {
public:
    explicit FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity = 16);

    // Renders frame `frame` at outWidth x outHeight (0 = the timeline's resolution).
    // Positions are in timeline pixels and scale with the output size.
    Result<VideoFrame> render(const Timeline& timeline, FrameIndex frame, int outWidth = 0, int outHeight = 0);

    static constexpr uint8_t kOfflineColor[3] = {140, 20, 40};

private:
    Status drawLayer(FloatFrame& canvas, const detail::Layer& layer, const Timeline& timeline, FrameIndex frame);

    const Lut* lut(const std::optional<LutRef>& ref);

    MediaResolver resolver_;
    DecoderPool pool_;
    LutCache luts_;
    std::set<std::filesystem::path> reportedLutFailures_;
};

// Every LUT file the timeline uses (clip grades, including stored versions, and the
// output LUT), without duplicates.
std::vector<std::filesystem::path> lutsUsedBy(const Timeline& timeline);
// Fails with the first LUT that would render (the output LUT and active, unbypassed
// clip grades) but cannot be loaded (missing or malformed file).
Status checkTimelineLuts(const Timeline& timeline);

}  // namespace up::render
