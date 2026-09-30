#pragma once

#include "codec/VideoFrame.h"
#include "core/Result.h"
#include "render/MediaSource.h"
#include "timeline/Timeline.h"

namespace up::render {

// Produces the picture of a timeline at a given frame.
//
// Every enabled video track is composited bottom (V1) to top over black, using each
// clip's transform evaluated at the frame: the source is first fitted to the
// timeline frame (aspect preserved), then scaled, rotated, moved, cropped and
// blended with its opacity (straight-alpha "over"). Layers under a fully opaque,
// frame-covering layer are skipped. Offline media renders as a layer of the offline
// colour so problems are visible rather than silently black.
class FrameCompositor {
public:
    explicit FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity = 16);

    // Renders frame `frame` at outWidth x outHeight (0 = the timeline's resolution).
    // Positions are in timeline pixels and scale with the output size.
    Result<VideoFrame> render(const Timeline& timeline, FrameIndex frame, int outWidth = 0, int outHeight = 0);

    static constexpr uint8_t kOfflineColor[3] = {140, 20, 40};

private:
    MediaResolver resolver_;
    DecoderPool pool_;
};

}  // namespace up::render
