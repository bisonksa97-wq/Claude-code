#include "app/SourceProject.h"

#include "media/MediaLibrary.h"
#include "timeline/EditOperations.h"

namespace up {

Result<Project> makeSourceProject(const Project& project, const std::string& mediaId) {
    const MediaItem* media = project.findMedia(mediaId);
    if (!media) return makeError(ErrorCode::NotFound, "media", "The media item does not exist in this project.");
    const Timeline* edited = project.activeTimeline();
    SequenceSettings settings = project.settings;
    if (edited) {
        settings.frameRate = edited->frameRate;
        settings.width = edited->width;
        settings.height = edited->height;
        settings.sampleRate = edited->sampleRate;
    }
    Project source;
    source.id = project.id + ":source";
    source.name = media->name;
    source.settings = settings;
    source.media.push_back(*media);
    Timeline tl = Timeline::create(media->name, settings.frameRate, settings.width, settings.height,
                                   settings.sampleRate, 1, 1);
    const FrameIndex length = media::lengthInFrames(media->info, settings.frameRate);
    Clip clip;
    clip.mediaId = media->id;
    clip.name = media->name;
    clip.duration = length > 0 ? length : secondsToFrames(5.0, settings.frameRate);
    clip.sourceLength = length;
    if (media->info.hasVideo) {
        auto r = ops::placeClip(tl, tl.tracks[0].id, clip, ops::EditMode::Overwrite);
        if (!r.ok()) return r.error();
    }
    if (media->info.hasAudio) {
        auto r = ops::placeClip(tl, tl.tracks[1].id, clip, ops::EditMode::Overwrite);
        if (!r.ok()) return r.error();
    }
    source.activeTimelineId = tl.id;
    source.timelines.push_back(std::move(tl));
    return source;
}

}  // namespace up
