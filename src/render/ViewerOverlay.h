#pragma once

#include "codec/VideoFrame.h"

namespace up::render {

// Exposure aids drawn over the viewer's picture (never rendered or exported).
//   Clipping: pixels with any channel at 254+ turn red, pixels with every channel
//     at 1 or below turn blue.
//   FalseColor: Rec.709 luma of the displayed signal in bands (percent of full scale):
//     < 2.5 purple, 2.5-4 blue, 38-42 green (18 % grey on a Rec.709 display),
//     52-56 pink (typical skin), 97-99 yellow, > 99 red; everything else is shown
//     as its own luma in grey.
enum class ViewerOverlay { None, Clipping, FalseColor };

void applyOverlay(VideoFrame& frame, ViewerOverlay overlay);

}  // namespace up::render
