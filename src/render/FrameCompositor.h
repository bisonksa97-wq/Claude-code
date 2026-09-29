#pragma once

#include "codec/VideoFrame.h"
#include "core/Result.h"
#include "render/MediaSource.h"
#include "timeline/Timeline.h"

namespace up::render {

// Produces the picture of a timeline at a given frame.
//
// Current model: the top-most enabled video track with an enabled clip at the
// frame wins (no blending yet). The source is scaled to fit the output while
// preserving aspect ratio (letter/pillar-boxed on black). Offline media renders
// as a solid offline colour so problems are visible rather than silently black.
class FrameCompositor {
public:
    explicit FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity = 16);

    // Renders frame `frame` at outWidth x outHeight (0 = the timeline's resolution).
    Result<VideoFrame> render(const Timeline& timeline, FrameIndex frame, int outWidth = 0, int outHeight = 0);

    static constexpr uint8_t kOfflineColor[3] = {140, 20, 40};

private:
    MediaResolver resolver_;
    DecoderPool pool_;
};

}  // namespace up::render
