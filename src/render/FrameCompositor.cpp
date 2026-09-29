#include "render/FrameCompositor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Log.h"

namespace up::render {

FrameCompositor::FrameCompositor(MediaResolver resolver, std::size_t decoderCapacity)
    : resolver_(std::move(resolver)), pool_(decoderCapacity) {}

Result<VideoFrame> FrameCompositor::render(const Timeline& timeline, FrameIndex frame, int outWidth, int outHeight) {
    if (outWidth <= 0 || outHeight <= 0) {
        outWidth = timeline.width;
        outHeight = timeline.height;
    }
    VideoFrame canvas(outWidth, outHeight);
    canvas.fill(0, 0, 0);

    const auto videoTracks = timeline.tracksOfKind(TrackKind::Video);
    for (auto it = videoTracks.rbegin(); it != videoTracks.rend(); ++it) {
        const Track& track = **it;
        if (!track.enabled) continue;
        const Clip* clip = track.clipAt(frame);
        if (!clip || !clip->enabled) continue;

        const MediaItem* media = resolver_ ? resolver_(clip->mediaId) : nullptr;
        if (!media || !media->online || !media->info.hasVideo) {
            if (media && !media->info.hasVideo) continue;  // audio-only media on a video track shows nothing
            canvas.fill(kOfflineColor[0], kOfflineColor[1], kOfflineColor[2]);
            return canvas;
        }

        // Fit the source into the canvas, preserving its aspect ratio.
        const double srcW = std::max(1, media->info.width);
        const double srcH = std::max(1, media->info.height);
        const double scale = std::min(outWidth / srcW, outHeight / srcH);
        const int w = std::max(2, static_cast<int>(std::lround(srcW * scale)));
        const int h = std::max(2, static_cast<int>(std::lround(srcH * scale)));
        const int x0 = (outWidth - std::min(w, outWidth)) / 2;
        const int y0 = (outHeight - std::min(h, outHeight)) / 2;

        auto decoder = pool_.video(clip->id, media->path);
        if (!decoder.ok()) {
            UP_LOG_WARN(log::sub::Render, decoder.error().message);
            canvas.fill(kOfflineColor[0], kOfflineColor[1], kOfflineColor[2]);
            return canvas;
        }
        // Sample an eighth of a frame into the display interval so rounding never lands on the previous frame.
        const double seconds = framesToSeconds(clip->sourceIn + (frame - clip->start), timeline.frameRate) +
                               0.5 / std::max(1.0, timeline.frameRate.toDouble() * 4);
        auto picture = decoder.value()->frameAt(seconds, w, h);
        if (!picture.ok()) return picture.error();
        const VideoFrame& src = picture.value();
        const int copyW = std::min(src.width, outWidth - x0);
        for (int y = 0; y < std::min(src.height, outHeight - y0); ++y) {
            std::memcpy(canvas.row(y0 + y) + static_cast<std::size_t>(x0) * 4, src.row(y),
                        static_cast<std::size_t>(copyW) * 4);
        }
        return canvas;
    }
    return canvas;
}

}  // namespace up::render
