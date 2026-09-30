#include "render/ExportJob.h"

#include <chrono>

#include "core/Log.h"
#include "render/AudioMixer.h"
#include "render/ColorManagement.h"
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
    if (options_.preset) {
        UP_TRY(validatePreset(*options_.preset));
        const std::string missing = presetUnavailableReason(*options_.preset);
        if (!missing.empty()) {
            return makeError(ErrorCode::NotFound, "export", "The preset '" + options_.preset->name + "' cannot be used: " + missing + ".",
                             "Choose another preset, or install an FFmpeg build that includes that encoder.");
        }
        settings = encodeSettingsFor(*options_.preset);
    } else {
        settings.videoCodec = options_.videoCodec;
        settings.crf = options_.crf;
        settings.videoBitrate = options_.videoBitrate;
    }
    settings.width = timeline->width;
    settings.height = timeline->height;
    settings.frameRate = timeline->frameRate;
    const ColorSpace outputSpace = timeline->outputSpace();
    const ColorTags tags = colorTagsFor(outputSpace);  // what the file says about its colour
    settings.colorPrimaries = tags.primaries;
    settings.colorTransfer = tags.transfer;
    settings.colorMatrix = tags.matrix;
    settings.audio = settings.audio && options_.includeAudio;
    settings.sampleRate = timeline->sampleRate;
    settings.channels = AudioMixer::kChannels;
    if (!settings.video && !settings.audio) {
        return makeError(ErrorCode::InvalidArgument, "export", "This export would contain neither video nor audio.",
                         "Include audio, or choose a preset with video.");
    }
    if (settings.video && isHdr(outputSpace.transfer)) {
        // HDR10 static metadata describing the mastering display (the output primaries).
        const auto xy = primariesChromaticities(outputSpace.primaries);
        HdrMetadata hdr;
        hdr.red = xy[0];
        hdr.green = xy[1];
        hdr.blue = xy[2];
        hdr.white = xy[3];
        hdr.maxLuminance = options_.masteringMaxLuminance;
        hdr.minLuminance = options_.masteringMinLuminance;
        hdr.maxCll = options_.maxCll;
        hdr.maxFall = options_.maxFall;
        settings.hdr = hdr;
    }
    const bool deep = settings.video && options_.preset && options_.preset->bitDepth() > 8;
    const bool sequence = options_.preset && options_.preset->imageSequence();

    // A missing LUT would silently change the delivered colours, so refuse up front.
    if (Status luts = checkTimelineLuts(*timeline); !luts.ok()) return luts;
    if (timeline->gradesBypassed)
        UP_LOG_WARN(log::sub::Render, "Exporting with all clip grades bypassed (Color > Bypass All Grades).");

    // Write to a temporary file (or folder, for image sequences) so a cancelled or failed
    // export never leaves a truncated output behind. File extensions are kept so FFmpeg
    // still picks the right container.
    std::error_code ec;
    const std::filesystem::path temp = options_.output.parent_path() /
        (options_.output.stem().string() + ".partial" + (sequence ? std::string() : options_.output.extension().string()));
    std::filesystem::path writerPath = temp;
    if (sequence) {
        if (std::filesystem::exists(options_.output, ec)) {
            return makeError(ErrorCode::Conflict, "export", "The folder '" + options_.output.filename().string() + "' already exists.",
                             "Image sequences are written into a new folder; choose another name or remove the old one.");
        }
        std::filesystem::remove_all(temp, ec);
        std::filesystem::create_directories(temp, ec);
        if (ec) {
            return makeError(ErrorCode::IoError, "export", "The folder for the image sequence could not be created.",
                             "Check write permissions.", ec.message());
        }
        writerPath = temp / (options_.output.filename().string() + "_%06d.png");
    }
    auto writer = MediaWriter::open(writerPath, settings);
    if (!writer.ok()) {
        if (sequence) std::filesystem::remove_all(temp, ec);
        return writer.error();
    }

    FrameCompositor compositor(resolverFor(project_));
    AudioMixer mixer(resolverFor(project_));
    std::vector<float> audio;
    ExportProgress state{0, out - in};
    const auto started = std::chrono::steady_clock::now();

    auto fail = [&](Status s) {
        writer.value().reset();
        std::filesystem::remove_all(temp, ec);
        return s;
    };

    for (FrameIndex f = in; f < out; ++f) {
        if (cancelled_) {
            return fail(makeError(ErrorCode::Cancelled, "render", "The export was cancelled.",
                                  "Start the export again when ready."));
        }
        Status s = Status::success();
        if (settings.video) {
            if (deep) {
                auto frame = compositor.renderFloat(*timeline, f);
                if (!frame.ok()) return fail(frame.error());
                s = writer.value()->writeVideo(toVideoFrame16(frame.value()));
            } else {
                auto frame = compositor.render(*timeline, f);
                if (!frame.ok()) return fail(frame.error());
                s = writer.value()->writeVideo(frame.value());
            }
            if (!s.ok()) return fail(s);
        }
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
