#include "render/FrameCompositor.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Log.h"
#include "render/Compositing.h"

namespace up::render {

namespace {

struct Layer {
    const Clip* clip = nullptr;
    const MediaItem* media = nullptr;  // null or offline -> offline colour
    bool offline = false;
    int sourceWidth = 0;
    int sourceHeight = 0;
    double posX = 0, posY = 0, scale = 1, rotation = 0, opacity = 1;
    double cropL = 0, cropR = 0, cropT = 0, cropB = 0;
};

double evaluate(const Clip& clip, ClipParam p, FrameIndex sourceFrame) {
    const ClipParamInfo& info = paramInfo(p);
    return std::clamp(clip.transform[p].at(sourceFrame), info.minimum, info.maximum);
}

// True when the layer is opaque and covers the whole timeline frame, hiding everything below.
bool coversFrame(const Layer& l, const Timeline& tl) {
    if (l.opacity < 1.0 || std::abs(std::fmod(l.rotation, 360.0)) > 1e-9 || l.cropL > 0 || l.cropR > 0 || l.cropT > 0 ||
        l.cropB > 0)
        return false;
    const double fit = std::min(static_cast<double>(tl.width) / l.sourceWidth, static_cast<double>(tl.height) / l.sourceHeight);
    const double w = l.sourceWidth * fit * l.scale;
    const double h = l.sourceHeight * fit * l.scale;
    const double left = tl.width / 2.0 + l.posX - w / 2.0;
    const double top = tl.height / 2.0 + l.posY - h / 2.0;
    return left <= 0.5 && top <= 0.5 && left + w >= tl.width - 0.5 && top + h >= tl.height - 0.5;
}

}  // namespace

FrameCompositor::FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity)
    : resolver_(std::move(resolver)), pool_(decoderCapacity) {}

Result<VideoFrame> FrameCompositor::render(const Timeline& timeline, FrameIndex frame, int outWidth, int outHeight) {
    if (outWidth <= 0 || outHeight <= 0) {
        outWidth = timeline.width;
        outHeight = timeline.height;
    }
    VideoFrame canvas(outWidth, outHeight);
    canvas.fill(0, 0, 0);

    // Collect visible layers bottom to top.
    std::vector<Layer> layers;
    for (const Track* track : timeline.tracksOfKind(TrackKind::Video)) {
        if (!track->enabled) continue;
        const Clip* clip = track->clipAt(frame);
        if (!clip || !clip->enabled) continue;
        const MediaItem* media = resolver_ ? resolver_(clip->mediaId) : nullptr;
        if (media && !media->info.hasVideo) continue;  // audio-only media on a video track shows nothing
        Layer l;
        l.clip = clip;
        l.media = media;
        l.offline = !media || !media->online;
        l.sourceWidth = media && media->info.width > 0 ? media->info.width : timeline.width;
        l.sourceHeight = media && media->info.height > 0 ? media->info.height : timeline.height;
        const FrameIndex sourceFrame = clip->toSource(frame);
        l.posX = evaluate(*clip, ClipParam::PositionX, sourceFrame);
        l.posY = evaluate(*clip, ClipParam::PositionY, sourceFrame);
        l.scale = evaluate(*clip, ClipParam::Scale, sourceFrame) / 100.0;
        l.rotation = evaluate(*clip, ClipParam::Rotation, sourceFrame);
        l.opacity = evaluate(*clip, ClipParam::Opacity, sourceFrame) / 100.0;
        l.cropL = evaluate(*clip, ClipParam::CropLeft, sourceFrame) / 100.0;
        l.cropR = evaluate(*clip, ClipParam::CropRight, sourceFrame) / 100.0;
        l.cropT = evaluate(*clip, ClipParam::CropTop, sourceFrame) / 100.0;
        l.cropB = evaluate(*clip, ClipParam::CropBottom, sourceFrame) / 100.0;
        if (l.opacity <= 0.0 || l.scale <= 0.0) continue;
        layers.push_back(l);
    }
    // Skip everything under the top-most layer that hides it completely.
    std::size_t first = 0;
    for (std::size_t i = layers.size(); i-- > 0;) {
        if (coversFrame(layers[i], timeline)) {
            first = i;
            break;
        }
    }

    const double outScale = static_cast<double>(outWidth) / timeline.width;
    for (std::size_t i = first; i < layers.size(); ++i) {
        const Layer& l = layers[i];
        const double fit = std::min(static_cast<double>(timeline.width) / l.sourceWidth,
                                    static_cast<double>(timeline.height) / l.sourceHeight);
        const double canvasPerSource = fit * l.scale * outScale;
        // Decode no larger than needed (and never above native size); upscaling happens in compositing.
        const double decodeScale = std::min(1.0, canvasPerSource);
        const int dw = std::max(2, static_cast<int>(std::lround(l.sourceWidth * decodeScale)));
        const int dh = std::max(2, static_cast<int>(std::lround(l.sourceHeight * decodeScale)));

        VideoFrame image;
        if (!l.offline) {
            auto decoder = pool_.video(l.clip->id, l.media->path);
            if (decoder.ok()) {
                // Sample an eighth of a frame into the display interval so rounding never lands on the previous frame.
                const double seconds = framesToSeconds(l.clip->toSource(frame), timeline.frameRate) +
                                       0.5 / std::max(1.0, timeline.frameRate.toDouble() * 4);
                auto picture = decoder.value()->frameAt(seconds, dw, dh);
                if (!picture.ok()) return picture.error();
                image = std::move(picture.value());
            } else {
                UP_LOG_WARN(log::sub::Render, decoder.error().message);
            }
        }
        if (image.empty()) {
            image = VideoFrame(dw, dh);
            image.fill(kOfflineColor[0], kOfflineColor[1], kOfflineColor[2]);
        }
        LayerPlacement placement;
        placement.centerX = outWidth / 2.0 + l.posX * outScale;
        placement.centerY = outHeight / 2.0 + l.posY * outScale;
        placement.scale = canvasPerSource * l.sourceWidth / image.width;
        placement.rotationDegrees = l.rotation;
        placement.opacity = l.opacity;
        placement.cropLeft = l.cropL;
        placement.cropRight = l.cropR;
        placement.cropTop = l.cropT;
        placement.cropBottom = l.cropB;
        compositeOver(canvas, image, placement);
    }
    return canvas;
}

}  // namespace up::render
