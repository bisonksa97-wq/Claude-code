#pragma once

#include <atomic>
#include <filesystem>
#include <functional>

#include "codec/MediaWriter.h"
#include "core/Result.h"
#include "project/Project.h"
#include "timeline/Timeline.h"

namespace up::render {

struct ExportOptions {
    std::filesystem::path output;
    std::string videoCodec;  // empty = default encoder
    int crf = 18;
    int64_t videoBitrate = 0;
    bool includeAudio = true;
    // Export range in timeline frames; outFrame <= 0 means "to the end of the timeline".
    FrameIndex inFrame = 0;
    FrameIndex outFrame = 0;
};

struct ExportProgress {
    FrameIndex framesDone = 0;
    FrameIndex framesTotal = 0;
};

// Renders a timeline to a file: compositor + mixer -> encoder, frame by frame.
// Deterministic for identical inputs and settings. Can run on a worker thread;
// the project/timeline must not be modified while it runs (pass copies).
class ExportJob {
public:
    using ProgressFn = std::function<void(const ExportProgress&)>;

    ExportJob(Project project, std::string timelineId, ExportOptions options);

    Status run(const ProgressFn& progress = {});
    void cancel() { cancelled_ = true; }
    bool cancelled() const { return cancelled_; }

private:
    Project project_;
    std::string timelineId_;
    ExportOptions options_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace up::render
