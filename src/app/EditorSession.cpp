#include "app/EditorSession.h"

#include <algorithm>
#include <map>

#include "app/ProjectCommands.h"
#include "core/AtomicFile.h"
#include "core/Id.h"
#include "core/Log.h"
#include "media/MediaLibrary.h"
#include "project/ProjectSerializer.h"

namespace up {
namespace fs = std::filesystem;

namespace {

constexpr const char* kSub = "app";

// Ids of `clipId` and its linked partners that sit on unlocked tracks.
std::vector<std::string> editableGroup(const Timeline& tl, const std::string& clipId) {
    std::vector<std::string> ids{clipId};
    for (const auto& id : tl.linkedClips(clipId)) {
        const Track* t = tl.trackOfClip(id);
        if (t && !t->locked) ids.push_back(id);
    }
    return ids;
}

Error clipNotFound(const std::string& clipId) {
    return makeError(ErrorCode::NotFound, "timeline", "The clip '" + clipId + "' does not exist.",
                     "Refresh the timeline view and try again.");
}

const Track* firstUnlocked(const Timeline& tl, TrackKind kind) {
    for (const Track* t : tl.tracksOfKind(kind))
        if (!t->locked) return t;
    return nullptr;
}

}  // namespace

EditorSession::EditorSession(Project project) : project_(std::move(project)) {
    history_.setListener([this] {
        if (listener_) listener_();
    });
}

std::unique_ptr<EditorSession> EditorSession::createNew(std::string name, SequenceSettings settings) {
    return std::unique_ptr<EditorSession>(new EditorSession(Project::create(std::move(name), settings)));
}

Result<std::unique_ptr<EditorSession>> EditorSession::open(const fs::path& path) {
    auto project = ProjectSerializer::load(path);
    if (!project.ok()) return project.error();
    std::unique_ptr<EditorSession> session(new EditorSession(std::move(project.value())));
    session->refreshMediaStatus();
    return session;
}

Result<std::unique_ptr<EditorSession>> EditorSession::openRecovery(const fs::path& recoveryFile,
                                                                   const fs::path& originalPath) {
    auto project = ProjectSerializer::load(recoveryFile);
    if (!project.ok()) return project.error();
    project.value().filePath = originalPath;
    std::unique_ptr<EditorSession> session(new EditorSession(std::move(project.value())));
    session->refreshMediaStatus();
    // The recovered state differs from what is on disk until the user saves.
    session->history_.markDirty();
    return session;
}

Timeline& EditorSession::timeline() {
    Timeline* t = project_.activeTimeline();
    if (!t) {
        project_.timelines.push_back(Timeline::create("Timeline 1", project_.settings.frameRate, project_.settings.width,
                                                      project_.settings.height, project_.settings.sampleRate));
        project_.activeTimelineId = project_.timelines.back().id;
        t = &project_.timelines.back();
    }
    return *t;
}

const Timeline& EditorSession::timeline() const {
    return const_cast<EditorSession*>(this)->timeline();
}

Status EditorSession::save() {
    if (project_.filePath.empty()) {
        return makeError(ErrorCode::InvalidArgument, kSub, "This project has not been saved yet.",
                         "Use Save As to choose a location.");
    }
    return saveAs(project_.filePath);
}

Status EditorSession::saveAs(const fs::path& path) {
    const fs::path previousAutosave = autosavePath();
    UP_TRY(ProjectSerializer::save(project_, path));
    history_.markClean();
    std::error_code ec;
    fs::remove(previousAutosave, ec);
    fs::remove(autosavePath(), ec);
    return Status::success();
}

fs::path EditorSession::recoveryDirectory() {
    std::error_code ec;
    return fs::temp_directory_path(ec) / "UltimatePost" / "Recovery";
}

fs::path EditorSession::autosavePath() const {
    if (!project_.filePath.empty()) {
        fs::path p = project_.filePath;
        p.replace_extension(".autosave.uproj");
        return p;
    }
    return recoveryDirectory() / (project_.id + ".uproj");
}

Status EditorSession::writeAutosave() {
    const fs::path path = autosavePath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // Relative media paths are computed against the autosave location, which is the
    // project folder for saved projects, so recovered projects resolve media the same way.
    UP_TRY(writeFileAtomically(path, ProjectSerializer::toJson(project_, path), /*keepBackup=*/false));
    UP_LOG_DEBUG(log::sub::Project, "Autosaved to " << path.string());
    return Status::success();
}

void EditorSession::discardAutosave() {
    std::error_code ec;
    fs::remove(autosavePath(), ec);
}

std::optional<fs::path> EditorSession::newerAutosaveFor(const fs::path& projectFile) {
    fs::path autosave = projectFile;
    autosave.replace_extension(".autosave.uproj");
    std::error_code ec;
    if (!fs::exists(autosave, ec)) return std::nullopt;
    if (fs::exists(projectFile, ec) && fs::last_write_time(autosave, ec) <= fs::last_write_time(projectFile, ec))
        return std::nullopt;
    return autosave;
}

ImportReport EditorSession::importMedia(const std::vector<fs::path>& paths) {
    ImportReport report;
    std::vector<MediaItem> items;
    const std::string binId = project_.bins.empty() ? std::string() : project_.bins.front().id;
    for (const auto& path : paths) {
        auto item = media::createMediaItem(path, binId);
        if (item.ok()) {
            report.importedIds.push_back(item.value().id);
            items.push_back(std::move(item.value()));
        } else {
            UP_LOG_WARN(log::sub::Media, item.error().toString());
            report.failures.push_back(item.error());
        }
    }
    if (!items.empty()) {
        Status s = history_.execute(std::make_unique<AddMediaCommand>(project_, std::move(items)));
        if (!s.ok()) {
            report.failures.push_back(s.error());
            report.importedIds.clear();
        }
    }
    return report;
}

std::size_t EditorSession::refreshMediaStatus() {
    const std::size_t offline = media::refreshOnlineState(project_);
    if (listener_) listener_();
    return offline;
}

Status EditorSession::relinkMedia(const std::string& mediaId, const fs::path& newPath) {
    const MediaItem* item = project_.findMedia(mediaId);
    if (!item) return makeError(ErrorCode::NotFound, "media", "The media item to relink does not exist.");
    MediaInfo info;
    UP_TRY(media::checkRelinkCandidate(*item, newPath, &info));
    return history_.execute(
        std::make_unique<RelinkMediaCommand>(project_, mediaId, fs::absolute(newPath).lexically_normal(), info));
}

Status EditorSession::editTimeline(const std::string& name, std::function<Status(Timeline&)> edit) {
    return history_.execute(std::make_unique<TimelineEditCommand>(project_, timeline().id, name, std::move(edit)));
}

Result<std::vector<std::string>> EditorSession::placeMedia(const std::string& mediaId, FrameIndex at,
                                                           ops::EditMode mode, std::string videoTrackId,
                                                           std::string audioTrackId, FrameIndex sourceIn,
                                                           FrameIndex duration) {
    const MediaItem* media = project_.findMedia(mediaId);
    if (!media) {
        return makeError(ErrorCode::NotFound, "media", "The media item does not exist in this project.",
                         "Import the file first.");
    }
    Timeline& tl = timeline();
    const FrameIndex length = media::lengthInFrames(media->info, tl.frameRate);
    if (duration <= 0) {
        duration = length > 0 ? length - sourceIn : secondsToFrames(5.0, tl.frameRate);
    }
    if (duration <= 0 || (length > 0 && sourceIn + duration > length) || sourceIn < 0) {
        return makeError(ErrorCode::OutOfRange, "timeline",
                         "'" + media->name + "' is too short for the requested range.",
                         "Choose an in/out range within the media.");
    }
    const bool wantVideo = media->info.hasVideo;
    const bool wantAudio = media->info.hasAudio;
    if (wantVideo && videoTrackId.empty()) {
        const Track* t = firstUnlocked(tl, TrackKind::Video);
        if (!t) return makeError(ErrorCode::Locked, "timeline", "All video tracks are locked.", "Unlock a video track.");
        videoTrackId = t->id;
    }
    if (wantAudio && audioTrackId.empty()) {
        const Track* t = firstUnlocked(tl, TrackKind::Audio);
        if (!t) return makeError(ErrorCode::Locked, "timeline", "All audio tracks are locked.", "Unlock an audio track.");
        audioTrackId = t->id;
    }

    std::vector<std::string> placed;
    const std::string linkId = wantVideo && wantAudio ? generateId() : std::string();
    Status s = editTimeline(mode == ops::EditMode::Insert ? "Insert Clip" : "Overwrite Clip", [&](Timeline& t) -> Status {
        placed.clear();
        if (mode == ops::EditMode::Insert) {
            for (const auto& track : t.tracks)
                if (!track.locked) UP_TRY(ops::insertGap(t, track.id, at, duration));
        }
        Clip clip;
        clip.mediaId = media->id;
        clip.name = media->name;
        clip.start = at;
        clip.duration = duration;
        clip.sourceIn = sourceIn;
        clip.sourceLength = length;
        clip.linkId = linkId;
        if (wantVideo) {
            auto id = ops::placeClip(t, videoTrackId, clip, ops::EditMode::Overwrite);
            if (!id.ok()) return id.error();
            placed.push_back(id.value());
        }
        if (wantAudio) {
            auto id = ops::placeClip(t, audioTrackId, clip, ops::EditMode::Overwrite);
            if (!id.ok()) return id.error();
            placed.push_back(id.value());
        }
        return Status::success();
    });
    if (!s.ok()) return s.error();
    return placed;
}

Result<std::vector<std::string>> EditorSession::appendMedia(const std::string& mediaId) {
    return placeMedia(mediaId, timeline().duration(), ops::EditMode::Overwrite);
}

Result<int> EditorSession::razorAt(FrameIndex frame) {
    int cuts = 0;
    Status s = editTimeline("Razor", [&](Timeline& t) -> Status {
        cuts = 0;
        std::map<std::string, std::string> newLinks;  // original link id -> link id for right-hand pieces
        std::vector<std::string> targets;
        for (const auto& track : t.tracks) {
            if (track.locked) continue;
            if (const Clip* c = track.clipAt(frame); c && c->start < frame) targets.push_back(c->id);
        }
        for (const auto& id : targets) {
            const Clip* c = t.clip(id);
            std::string rightLink;
            if (!c->linkId.empty()) {
                auto [it, inserted] = newLinks.try_emplace(c->linkId, generateId());
                rightLink = it->second;
            }
            auto r = ops::razor(t, id, frame, rightLink);
            if (!r.ok()) return r.error();
            ++cuts;
        }
        if (cuts == 0) {
            return makeError(ErrorCode::InvalidArgument, "timeline", "There is no clip to cut at the playhead.",
                             "Move the playhead over a clip (not on its first frame) and try again.");
        }
        return Status::success();
    });
    if (!s.ok()) return s.error();
    return cuts;
}

Status EditorSession::liftClip(const std::string& clipId) {
    return editTimeline("Lift", [&](Timeline& t) -> Status {
        if (!t.clip(clipId)) return clipNotFound(clipId);
        for (const auto& id : editableGroup(t, clipId)) UP_TRY(ops::lift(t, id));
        return Status::success();
    });
}

Status EditorSession::rippleDeleteClip(const std::string& clipId) {
    return editTimeline("Ripple Delete", [&](Timeline& t) -> Status {
        if (!t.clip(clipId)) return clipNotFound(clipId);
        for (const auto& id : editableGroup(t, clipId)) UP_TRY(ops::rippleDelete(t, id));
        return Status::success();
    });
}

Status EditorSession::trimClip(const std::string& clipId, ops::Edge edge, FrameIndex delta, ops::TrimMode mode) {
    const char* name = mode == ops::TrimMode::Ripple ? "Ripple Trim" : "Trim";
    return editTimeline(name, [&](Timeline& t) -> Status {
        const Clip* c = t.clip(clipId);
        if (!c) return clipNotFound(clipId);
        const FrameIndex edgePos = edge == ops::Edge::In ? c->start : c->end();
        for (const auto& id : editableGroup(t, clipId)) {
            const Clip* p = t.clip(id);
            // Only partners whose edge lines up with the edited edge follow the trim.
            if ((edge == ops::Edge::In ? p->start : p->end()) != edgePos) continue;
            UP_TRY(ops::trim(t, id, edge, delta, mode));
        }
        return Status::success();
    });
}

Status EditorSession::rollEdit(const std::string& leftClipId, FrameIndex delta) {
    return editTimeline("Roll Edit", [&](Timeline& t) -> Status {
        if (!t.clip(leftClipId)) return clipNotFound(leftClipId);
        UP_TRY(ops::roll(t, leftClipId, delta));
        for (const auto& id : t.linkedClips(leftClipId)) {
            const Track* tr = t.trackOfClip(id);
            const Clip* p = t.clip(id);
            if (tr->locked) continue;
            // Partners roll only when they also have an adjacent clip after them.
            const bool adjacent = std::any_of(tr->clips.begin(), tr->clips.end(),
                                              [&](const Clip& c) { return c.start == p->end(); });
            if (adjacent) UP_TRY(ops::roll(t, id, delta));
        }
        return Status::success();
    });
}

Status EditorSession::slipClip(const std::string& clipId, FrameIndex delta) {
    return editTimeline("Slip", [&](Timeline& t) -> Status {
        if (!t.clip(clipId)) return clipNotFound(clipId);
        for (const auto& id : editableGroup(t, clipId)) UP_TRY(ops::slip(t, id, delta));
        return Status::success();
    });
}

Status EditorSession::slideClip(const std::string& clipId, FrameIndex delta) {
    return editTimeline("Slide", [&](Timeline& t) -> Status {
        if (!t.clip(clipId)) return clipNotFound(clipId);
        for (const auto& id : editableGroup(t, clipId)) UP_TRY(ops::slide(t, id, delta));
        return Status::success();
    });
}

Status EditorSession::moveClip(const std::string& clipId, const std::string& trackId, FrameIndex newStart) {
    return editTimeline("Move Clip", [&](Timeline& t) -> Status {
        const Clip* c = t.clip(clipId);
        if (!c) return clipNotFound(clipId);
        const FrameIndex delta = newStart - c->start;
        // Capture partner positions before anything moves.
        std::vector<std::pair<std::string, std::pair<std::string, FrameIndex>>> partners;
        for (const auto& id : editableGroup(t, clipId)) {
            if (id == clipId) continue;
            partners.push_back({id, {t.trackOfClip(id)->id, t.clip(id)->start + delta}});
        }
        UP_TRY(ops::moveClip(t, clipId, trackId, newStart));
        for (const auto& [id, dest] : partners) {
            if (!t.clip(id)) continue;  // overwritten by the main move (same track)
            UP_TRY(ops::moveClip(t, id, dest.first, dest.second));
        }
        return Status::success();
    });
}

Status EditorSession::setTrackState(const std::string& trackId, const TrackState& state) {
    return editTimeline("Change Track", [&](Timeline& t) -> Status {
        Track* track = t.track(trackId);
        if (!track) {
            return makeError(ErrorCode::NotFound, "timeline", "The track does not exist.",
                             "Refresh the timeline view and try again.");
        }
        track->enabled = state.enabled;
        track->locked = state.locked;
        track->muted = state.muted;
        track->solo = state.solo;
        track->gainDb = state.gainDb;
        return Status::success();
    });
}

}  // namespace up
