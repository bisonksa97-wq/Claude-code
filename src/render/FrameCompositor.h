#pragma once

#include "codec/VideoFrame.h"
#include "core/Result.h"
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
class FrameCompositor {
public:
    explicit FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity = 16);

    // Renders frame `frame` at outWidth x outHeight (0 = the timeline's resolution).
    // Positions are in timeline pixels and scale with the output size.
    Result<VideoFrame> render(const Timeline& timeline, FrameIndex frame, int outWidth = 0, int outHeight = 0);

    static constexpr uint8_t kOfflineColor[3] = {140, 20, 40};

private:
    Status drawLayer(VideoFrame& canvas, const detail::Layer& layer, const Timeline& timeline, FrameIndex frame);

    MediaResolver resolver_;
    DecoderPool pool_;
};

}  // namespace up::render
