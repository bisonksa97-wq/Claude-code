#include "render/ExportJob.h"

#include <chrono>

#include "core/Log.h"
#include "render/AudioMixer.h"
#include "render/FrameCompositor.h"

namespace up::render {

ExportJob::ExportJob(Project project, std::string timelineId, ExportOptions options)
    : project_(std::move(project)), timelineId_(std::move(timelineId)), options_(std::move(options)) {}

Status ExportJob::run(const ProgressFn& progress) {
    const Timeline* timeline = project_.findTimeline(timelineId_);
    if (!timeline) {
        return makeError(ErrorCode::NotFound, "render", "The timeline to export no longer exists.",
                         "Choose another timeline.");
    }
    const FrameIndex in = std::max<FrameIndex>(0, options_.inFrame);
    const FrameIndex out = options_.outFrame > 0 ? options_.outFrame : timeline->duration();
    if (out <= in) {
        return makeError(ErrorCode::InvalidArgument, "render", "There is nothing to export: the timeline is empty.",
                         "Add clips to the timeline, or check the in/out range.");
    }
    for (const auto& track : timeline->tracks) {
        for (const auto& clip : track.clips) {
            const MediaItem* m = project_.findMedia(clip.mediaId);
            if (!m || !m->online) {
                UP_LOG_WARN(log::sub::Render, "Clip '" << clip.name << "' references offline media; it will render as "
                                                          "an offline frame / silence.");
            }
        }
    }

    EncodeSettings settings;
    settings.width = timeline->width;
    settings.height = timeline->height;
    settings.frameRate = timeline->frameRate;
    settings.videoCodec = options_.videoCodec;
    settings.crf = options_.crf;
    settings.videoBitrate = options_.videoBitrate;
    settings.audio = options_.includeAudio;
    settings.sampleRate = timeline->sampleRate;
    settings.channels = AudioMixer::kChannels;

    // A missing LUT would silently change the delivered colours, so refuse up front.
    if (Status luts = checkTimelineLuts(*timeline); !luts.ok()) return luts;
    if (timeline->gradesBypassed)
        UP_LOG_WARN(log::sub::Render, "Exporting with all clip grades bypassed (Color > Bypass All Grades).");

    // Write to a temporary file so a cancelled or failed export never leaves a truncated output behind.
    // The extension is kept so FFmpeg still picks the right container.
    const std::filesystem::path temp = options_.output.parent_path() /
        (options_.output.stem().string() + ".partial" + options_.output.extension().string());
    auto writer = MediaWriter::open(temp, settings);
    if (!writer.ok()) return writer.error();

    FrameCompositor compositor(resolverFor(project_));
    AudioMixer mixer(resolverFor(project_));
    std::vector<float> audio;
    ExportProgress state{0, out - in};
    const auto started = std::chrono::steady_clock::now();

    auto fail = [&](Status s) {
        std::error_code ec;
        writer.value().reset();
        std::filesystem::remove(temp, ec);
        return s;
    };

    for (FrameIndex f = in; f < out; ++f) {
        if (cancelled_) {
            return fail(makeError(ErrorCode::Cancelled, "render", "The export was cancelled.",
                                  "Start the export again when ready."));
        }
        auto frame = compositor.render(*timeline, f);
        if (!frame.ok()) return fail(frame.error());
        Status s = writer.value()->writeVideo(frame.value());
        if (!s.ok()) return fail(s);
        if (settings.audio) {
            const int64_t s0 = frameToSample(f, timeline->frameRate, timeline->sampleRate);
            const int64_t s1 = frameToSample(f + 1, timeline->frameRate, timeline->sampleRate);
            s = mixer.mix(*timeline, s0, s1 - s0, audio);
            if (!s.ok()) return fail(s);
            s = writer.value()->writeAudio(audio.data(), s1 - s0);
            if (!s.ok()) return fail(s);
        }
        ++state.framesDone;
        if (progress) progress(state);
    }
    Status s = writer.value()->finish();
    if (!s.ok()) return fail(s);
    writer.value().reset();

    std::error_code ec;
    std::filesystem::rename(temp, options_.output, ec);
    if (ec) {
        return makeError(ErrorCode::IoError, "render", "The export finished but could not be moved into place.",
                         "Check that '" + options_.output.string() + "' is not open in another program.", ec.message());
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    UP_LOG_INFO(log::sub::Render, "Exported " << state.framesTotal << " frames to " << options_.output.string() << " in "
                                              << secs << "s (" << (secs > 0 ? static_cast<double>(state.framesTotal) / secs : 0.0) << " fps)");
    return Status::success();
}

}  // namespace up::render
