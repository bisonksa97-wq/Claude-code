#pragma once

#include "codec/VideoFrame.h"

namespace up::render {

// Where and how a source image lands on the canvas.
struct LayerPlacement {
    double centerX = 0.0;          // canvas pixel the source centre maps to
    double centerY = 0.0;
    double scale = 1.0;            // canvas pixels per source pixel
    double rotationDegrees = 0.0;  // clockwise, about the source centre
    double opacity = 1.0;          // 0..1
    // Fractions (0..1) of the source removed from each edge.
    double cropLeft = 0.0;
    double cropRight = 0.0;
    double cropTop = 0.0;
    double cropBottom = 0.0;
};

// Composites `source` over `canvas` (straight alpha, "over" operator) with the given
// placement. Unrotated, unscaled layers take an exact row-copy fast path (position
// rounded to whole pixels); everything else is inverse-mapped with bilinear sampling.
// The canvas stays opaque.
void compositeOver(VideoFrame& canvas, const VideoFrame& source, const LayerPlacement& placement);

}  // namespace up::render
