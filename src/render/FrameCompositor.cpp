#include "render/FrameCompositor.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Log.h"
#include "render/ColorGrading.h"
#include "render/ColorManagement.h"
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

// Weights that sum to less than one (a dip) fade towards `black`, the encoded value
// of linear 0 in the timeline space (not 0 for log encodings).
void mix(FloatFrame& canvas, const FloatFrame& withOutgoing, const FloatFrame& withIncoming,
         const transitions::VideoWeights& w, float black) {
    parallelRows(canvas.height, static_cast<std::size_t>(canvas.width) * 4, [&](int y0, int y1) {
        const std::size_t end = static_cast<std::size_t>(y1) * canvas.width * 4;
        for (std::size_t i = static_cast<std::size_t>(y0) * canvas.width * 4; i < end; i += 4) {
            for (std::size_t c = 0; c < 3; ++c) {
                const double v = black + (withOutgoing.pixels[i + c] - black) * w.outgoing +
                                 (canvas.pixels[i + c] - black) * w.below + (withIncoming.pixels[i + c] - black) * w.incoming;
                canvas.pixels[i + c] = static_cast<float>(v);
            }
        }
    });
}

}  // namespace

Status FrameCompositor::drawLayer(FloatFrame& canvas, const Layer& l, const Timeline& timeline, FrameIndex frame) {
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
    FloatFrame layer;
    if (image.empty()) {
        layer = FloatFrame(dw, dh);  // offline placeholder, in the timeline space
        layer.fill(kOfflineColor[0] / 255.0f, kOfflineColor[1] / 255.0f, kOfflineColor[2] / 255.0f);
    } else {
        // Into the timeline colour space, then grade there, before transform and compositing.
        layer = ColorConversion(mediaColorSpace(*l.media), timeline.colorSpace).convert(image);
        if (!timeline.gradesBypassed && !l.clip->gradeBypass && !l.clip->grade.isIdentity()) {
            applyGrade(layer, evaluateGrade(l.clip->grade, l.clip->toSource(frame)), l.clip->grade.curves,
                       lut(l.clip->grade.lut), timeline.colorSpace.transfer);
        }
    }
    LayerPlacement placement;
    placement.centerX = outWidth / 2.0 + l.posX * outScale;
    placement.centerY = outHeight / 2.0 + l.posY * outScale;
    placement.scale = canvasPerSource * l.sourceWidth / layer.width;
    placement.rotationDegrees = l.rotation;
    placement.opacity = l.opacity;
    placement.cropLeft = l.cropL;
    placement.cropRight = l.cropR;
    placement.cropTop = l.cropT;
    placement.cropBottom = l.cropB;
    compositeOver(canvas, layer, placement);
    return Status::success();
}

Result<VideoFrame> FrameCompositor::render(const Timeline& timeline, FrameIndex frame, int outWidth, int outHeight) {
    if (outWidth <= 0 || outHeight <= 0) {
        outWidth = timeline.width;
        outHeight = timeline.height;
    }
    // Everything is composited in float, in the timeline colour space, over its black.
    const auto black = static_cast<float>(encodeTransfer(timeline.colorSpace.transfer, 0.0));
    FloatFrame canvas(outWidth, outHeight);
    canvas.fill(black, black, black);
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
        FloatFrame withOutgoing = canvas;
        FloatFrame withIncoming = canvas;
        if (region.outgoing) {
            if (auto l = makeLayer(*region.outgoing, resolve(*region.outgoing), timeline, frame))
                UP_TRY(drawLayer(withOutgoing, *l, timeline, frame));
        }
        if (region.incoming) {
            if (auto l = makeLayer(*region.incoming, resolve(*region.incoming), timeline, frame))
                UP_TRY(drawLayer(withIncoming, *l, timeline, frame));
        }
        mix(canvas, withOutgoing, withIncoming, transitions::videoWeights(region.kind, plan.frame.progress), black);
    }
    // Output transform, then the output LUT, then the one and only quantisation.
    ColorConversion(timeline.colorSpace, timeline.outputSpace()).apply(canvas);
    if (const Lut* output = lut(timeline.outputLut)) applyLut(canvas, *output);
    return toVideoFrame(canvas);
}

const Lut* FrameCompositor::lut(const std::optional<LutRef>& ref) {
    if (!ref) return nullptr;
    auto loaded = luts_.get(ref->path);
    if (!loaded.ok()) {
        if (reportedLutFailures_.insert(ref->path).second) UP_LOG_WARN(log::sub::Render, loaded.error().toString());
        return nullptr;
    }
    reportedLutFailures_.erase(ref->path);
    return loaded.value().get();  // owned by the cache until the file changes
}

std::vector<std::filesystem::path> lutsUsedBy(const Timeline& timeline) {
    std::vector<std::filesystem::path> out;
    auto add = [&](const std::optional<LutRef>& ref) {
        if (ref && std::find(out.begin(), out.end(), ref->path) == out.end()) out.push_back(ref->path);
    };
    add(timeline.outputLut);
    for (const auto& track : timeline.tracks) {
        for (const auto& clip : track.clips) {
            add(clip.grade.lut);
            for (const auto& v : clip.gradeVersions) add(v.grade.lut);
        }
    }
    return out;
}

Status checkTimelineLuts(const Timeline& timeline) {
    // Only what renders matters: the output LUT and the active, unbypassed grades.
    std::vector<std::filesystem::path> paths;
    if (timeline.outputLut) paths.push_back(timeline.outputLut->path);
    for (const auto& track : timeline.tracks) {
        if (track.kind != TrackKind::Video || timeline.gradesBypassed) continue;
        for (const auto& clip : track.clips)
            if (clip.grade.lut && !clip.gradeBypass) paths.push_back(clip.grade.lut->path);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    for (const auto& path : paths) {
        auto lut = loadCubeLut(path);
        if (!lut.ok()) return lut.error();
    }
    return Status::success();
}

}  // namespace up::render
