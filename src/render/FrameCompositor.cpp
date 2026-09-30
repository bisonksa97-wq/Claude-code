#include "render/FrameCompositor.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Log.h"
#include "render/Compositing.h"
#include "timeline/Transitions.h"

namespace up::render {

namespace detail {

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

}  // namespace detail

using detail::Layer;
using detail::coversFrame;
using detail::evaluate;

FrameCompositor::FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity)
    : resolver_(std::move(resolver)), pool_(decoderCapacity) {}

namespace {

// Builds the layer for `clip` at timeline `frame` (which may lie in a handle during a transition).
std::optional<Layer> makeLayer(const Clip& clip, const MediaItem* media, const Timeline& timeline, FrameIndex frame) {
    if (!clip.enabled) return std::nullopt;
    if (media && !media->info.hasVideo) return std::nullopt;  // audio-only media on a video track shows nothing
    Layer l;
    l.clip = &clip;
    l.media = media;
    l.offline = !media || !media->online;
    l.sourceWidth = media && media->info.width > 0 ? media->info.width : timeline.width;
    l.sourceHeight = media && media->info.height > 0 ? media->info.height : timeline.height;
    const FrameIndex sourceFrame = clip.toSource(frame);
    l.posX = evaluate(clip, ClipParam::PositionX, sourceFrame);
    l.posY = evaluate(clip, ClipParam::PositionY, sourceFrame);
    l.scale = evaluate(clip, ClipParam::Scale, sourceFrame) / 100.0;
    l.rotation = evaluate(clip, ClipParam::Rotation, sourceFrame);
    l.opacity = evaluate(clip, ClipParam::Opacity, sourceFrame) / 100.0;
    l.cropL = evaluate(clip, ClipParam::CropLeft, sourceFrame) / 100.0;
    l.cropR = evaluate(clip, ClipParam::CropRight, sourceFrame) / 100.0;
    l.cropT = evaluate(clip, ClipParam::CropTop, sourceFrame) / 100.0;
    l.cropB = evaluate(clip, ClipParam::CropBottom, sourceFrame) / 100.0;
    if (l.opacity <= 0.0 || l.scale <= 0.0) return std::nullopt;
    return l;
}

void mix(VideoFrame& canvas, const VideoFrame& withOutgoing, const VideoFrame& withIncoming,
         const transitions::VideoWeights& w) {
    for (std::size_t i = 0; i < canvas.pixels.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            const double v = withOutgoing.pixels[i + c] * w.outgoing + canvas.pixels[i + c] * w.below +
                             withIncoming.pixels[i + c] * w.incoming;
            canvas.pixels[i + c] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
        }
    }
}

}  // namespace

Status FrameCompositor::drawLayer(VideoFrame& canvas, const Layer& l, const Timeline& timeline, FrameIndex frame) {
    const int outWidth = canvas.width;
    const int outHeight = canvas.height;
    const double outScale = static_cast<double>(outWidth) / timeline.width;
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
            auto picture = decoder.value()->frameAt(std::max(0.0, seconds), dw, dh);
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
    return Status::success();
}

Result<VideoFrame> FrameCompositor::render(const Timeline& timeline, FrameIndex frame, int outWidth, int outHeight) {
    if (outWidth <= 0 || outHeight <= 0) {
        outWidth = timeline.width;
        outHeight = timeline.height;
    }
    VideoFrame canvas(outWidth, outHeight);
    canvas.fill(0, 0, 0);
    auto resolve = [&](const Clip& c) { return resolver_ ? resolver_(c.mediaId) : nullptr; };

    // What each enabled video track shows at this frame, bottom (V1) to top.
    struct TrackPlan {
        std::vector<transitions::Region> regions;
        transitions::TrackFrame frame;
        std::optional<Layer> layer;  // the single layer when not in a transition
    };
    std::vector<TrackPlan> plans;
    for (const Track* track : timeline.tracksOfKind(TrackKind::Video)) {
        if (!track->enabled) continue;
        TrackPlan plan;
        plan.regions = transitions::regions(*track);
        plan.frame = transitions::evaluate(*track, plan.regions, frame);
        if (!plan.frame.region) {
            if (!plan.frame.clip) continue;
            plan.layer = makeLayer(*plan.frame.clip, resolve(*plan.frame.clip), timeline, frame);
            if (!plan.layer) continue;
        }
        plans.push_back(std::move(plan));
    }
    // Skip everything under the top-most layer that hides it completely (never a transition).
    std::size_t first = 0;
    for (std::size_t i = plans.size(); i-- > 0;) {
        if (plans[i].layer && coversFrame(*plans[i].layer, timeline)) {
            first = i;
            break;
        }
    }

    for (std::size_t i = first; i < plans.size(); ++i) {
        TrackPlan& plan = plans[i];
        if (plan.layer) {
            UP_TRY(drawLayer(canvas, *plan.layer, timeline, frame));
            continue;
        }
        // Transition: mix "below + outgoing", "below" and "below + incoming".
        // The regions vector lives in `plan`, so the region pointer stays valid here.
        const transitions::Region& region = *plan.frame.region;
        VideoFrame withOutgoing = canvas;
        VideoFrame withIncoming = canvas;
        if (region.outgoing) {
            if (auto l = makeLayer(*region.outgoing, resolve(*region.outgoing), timeline, frame))
                UP_TRY(drawLayer(withOutgoing, *l, timeline, frame));
        }
        if (region.incoming) {
            if (auto l = makeLayer(*region.incoming, resolve(*region.incoming), timeline, frame))
                UP_TRY(drawLayer(withIncoming, *l, timeline, frame));
        }
        mix(canvas, withOutgoing, withIncoming, transitions::videoWeights(region.kind, plan.frame.progress));
    }
    return canvas;
}

}  // namespace up::render
