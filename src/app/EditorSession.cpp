#include "app/EditorSession.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "app/ProjectCommands.h"
#include "core/AtomicFile.h"
#include "core/Id.h"
#include "core/Log.h"
#include "media/MediaLibrary.h"
#include "project/ProjectSerializer.h"
#include "core/Timecode.h"
#include "timeline/ThreePointEdit.h"
#include "timeline/Transitions.h"

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
                                                           FrameIndex duration, bool useVideo, bool useAudio) {
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
    const bool wantVideo = media->info.hasVideo && useVideo;
    const bool wantAudio = media->info.hasAudio && useAudio;
    if (!wantVideo && !wantAudio) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "Nothing to edit in: no stream of '" + media->name +
                                                                     "' is selected.",
                         "Enable a video or audio source target.");
    }
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

Status EditorSession::liftClip(const std::string& clipId) { return liftClips({clipId}); }

Status EditorSession::rippleDeleteClip(const std::string& clipId) { return rippleDeleteClips({clipId}); }

namespace {

// The clips plus their linked partners on unlocked tracks, without duplicates.
Result<std::vector<std::string>> expandSelection(const Timeline& t, const std::vector<std::string>& clipIds) {
    std::vector<std::string> out;
    for (const auto& id : clipIds) {
        if (!t.clip(id)) return clipNotFound(id);
        for (const auto& member : editableGroup(t, id))
            if (std::find(out.begin(), out.end(), member) == out.end()) out.push_back(member);
    }
    if (out.empty()) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "No clips are selected.", "Select a clip first.");
    }
    return out;
}

}  // namespace

Status EditorSession::liftClips(const std::vector<std::string>& clipIds) {
    return editTimeline(clipIds.size() > 1 ? "Lift Clips" : "Lift", [&](Timeline& t) -> Status {
        auto ids = expandSelection(t, clipIds);
        if (!ids.ok()) return ids.error();
        for (const auto& id : ids.value()) UP_TRY(ops::lift(t, id));
        return Status::success();
    });
}

Status EditorSession::rippleDeleteClips(const std::vector<std::string>& clipIds) {
    return editTimeline(clipIds.size() > 1 ? "Ripple Delete Clips" : "Ripple Delete", [&](Timeline& t) -> Status {
        auto ids = expandSelection(t, clipIds);
        if (!ids.ok()) return ids.error();
        // Latest first, so each ripple leaves the positions of the remaining clips valid.
        std::vector<std::string> order = ids.value();
        std::sort(order.begin(), order.end(),
                  [&](const std::string& a, const std::string& b) { return t.clip(a)->start > t.clip(b)->start; });
        for (const auto& id : order) UP_TRY(ops::rippleDelete(t, id));
        return Status::success();
    });
}

Status EditorSession::moveClips(const std::vector<std::string>& clipIds, FrameIndex delta, int trackShift,
                                TrackKind trackShiftKind) {
    return editTimeline(clipIds.size() > 1 ? "Move Clips" : "Move Clip", [&](Timeline& t) -> Status {
        auto ids = expandSelection(t, clipIds);
        if (!ids.ok()) return ids.error();
        struct Planned {
            Clip clip;
            std::string track;
        };
        std::vector<Planned> planned;
        for (const auto& id : ids.value()) {
            const Track* from = t.trackOfClip(id);
            std::string dest = from->id;
            if (trackShift != 0 && from->kind == trackShiftKind) {
                const auto list = t.trackIdsOfKind(from->kind);
                const auto index = (std::find(list.begin(), list.end(), from->id) - list.begin()) + trackShift;
                if (index < 0 || index >= static_cast<std::ptrdiff_t>(list.size())) {
                    return makeError(ErrorCode::OutOfRange, "timeline", "There is no track to move the clips to.",
                                     "Move them fewer tracks, or add a track first.");
                }
                dest = list[static_cast<std::size_t>(index)];
            }
            Clip moved = *t.clip(id);
            moved.start += delta;
            if (moved.start < 0) {
                return makeError(ErrorCode::OutOfRange, "timeline", "Clips cannot be moved before the start of the timeline.",
                                 "Use a smaller move.");
            }
            planned.push_back({std::move(moved), dest});
        }
        // Take every moved clip out first so they cannot overwrite each other.
        for (const auto& p : planned) UP_TRY(ops::lift(t, p.clip.id));
        for (auto& p : planned) {
            auto placed = ops::placeClip(t, p.track, p.clip, ops::EditMode::Overwrite);
            if (!placed.ok()) return placed.error();
        }
        return Status::success();
    });
}

std::vector<std::string> EditorSession::clipsFrom(FrameIndex frame) const {
    std::vector<std::string> out;
    for (const auto& track : timeline().tracks) {
        if (track.locked) continue;
        for (const auto& c : track.clips)
            if (c.start >= frame) out.push_back(c.id);
    }
    return out;
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

Status EditorSession::setMediaMarks(const std::string& mediaId, std::optional<FrameIndex> in,
                                    std::optional<FrameIndex> out) {
    const MediaItem* media = project_.findMedia(mediaId);
    if (!media) return makeError(ErrorCode::NotFound, "media", "The media item does not exist in this project.");
    const FrameRate rate = timeline().frameRate;
    const FrameIndex length = media::lengthInFrames(media->info, rate);
    if ((in && *in < 0) || (out && *out <= 0) || (in && out && *out <= *in) || (length > 0 && out && *out > length) ||
        (length > 0 && in && *in >= length)) {
        return makeError(ErrorCode::OutOfRange, "media", "The source marks must lie inside the media with in before out.",
                         "Move the playhead inside the clip before marking.");
    }
    MediaItem updated = *media;
    updated.markIn = in ? std::optional<double>(framesToSeconds(*in, rate)) : std::nullopt;
    updated.markOut = out ? std::optional<double>(framesToSeconds(*out, rate)) : std::nullopt;
    return history_.execute(std::make_unique<UpdateMediaCommand>(project_, std::move(updated), "Mark Source"));
}

std::pair<std::optional<FrameIndex>, std::optional<FrameIndex>> EditorSession::mediaMarks(const std::string& mediaId) const {
    const MediaItem* media = project_.findMedia(mediaId);
    if (!media) return {};
    const FrameRate rate = timeline().frameRate;
    auto toFrames = [&](const std::optional<double>& s) -> std::optional<FrameIndex> {
        if (!s) return std::nullopt;
        return secondsToFrames(*s, rate);
    };
    return {toFrames(media->markIn), toFrames(media->markOut)};
}

Status EditorSession::setTimelineMarks(std::optional<FrameIndex> in, std::optional<FrameIndex> out) {
    if ((in && *in < 0) || (in && out && *out <= *in) || (out && *out <= 0)) {
        return makeError(ErrorCode::OutOfRange, "timeline", "The timeline in mark must come before the out mark.",
                         "Set the marks again in order.");
    }
    return editTimeline("Mark Timeline", [&](Timeline& t) -> Status {
        t.markIn = in;
        t.markOut = out;
        return Status::success();
    });
}

Status EditorSession::setTrackTargets(const std::string& videoTrackId, const std::string& audioTrackId) {
    return editTimeline("Target Tracks", [&](Timeline& t) -> Status {
        t.videoTarget = videoTrackId;
        t.audioTarget = audioTrackId;
        return Status::success();  // validate() rejects missing tracks or wrong kinds
    });
}

Result<EditorSession::EditResult> EditorSession::threePointEdit(const std::string& mediaId, ops::EditMode mode,
                                                                FrameIndex playhead) {
    const MediaItem* media = project_.findMedia(mediaId);
    if (!media) return makeError(ErrorCode::NotFound, "media", "The media item does not exist in this project.");
    const Timeline& tl = timeline();
    const bool useVideo = media->info.hasVideo && !tl.videoTarget.empty();
    const bool useAudio = media->info.hasAudio && !tl.audioTarget.empty();
    if (!useVideo && !useAudio) {
        return makeError(ErrorCode::InvalidArgument, "timeline",
                         "No target track is enabled for the streams of '" + media->name + "'.",
                         "Click a track's target box in the timeline header to patch the source to it.");
    }
    const auto [srcIn, srcOut] = mediaMarks(mediaId);
    ops::ThreePointInput input;
    input.sourceIn = srcIn;
    input.sourceOut = srcOut;
    input.recordIn = tl.markIn;
    input.recordOut = tl.markOut;
    input.playhead = playhead;
    input.sourceLength = media::lengthInFrames(media->info, tl.frameRate);
    input.defaultDuration = secondsToFrames(5.0, tl.frameRate);
    auto resolved = ops::resolveThreePointEdit(input);
    if (!resolved.ok()) return resolved.error();
    const ops::ThreePointResult r = resolved.value();

    Transaction tx(history_, mode == ops::EditMode::Insert ? "Insert Edit" : "Overwrite Edit");
    auto placed = placeMedia(mediaId, r.recordIn, mode, tl.videoTarget, tl.audioTarget, r.sourceIn, r.duration,
                             useVideo, useAudio);
    if (!placed.ok()) return placed.error();
    if (tl.markIn || tl.markOut) UP_TRY(setTimelineMarks(std::nullopt, std::nullopt));
    tx.commit();
    return EditResult{placed.value(), r.recordIn, r.recordOut()};
}

// --- Markers ----------------------------------------------------------------------------

namespace {

Marker makeMarker(FrameIndex frame, std::string name, MarkerColor color, std::string comment, FrameIndex duration) {
    Marker m;
    m.id = generateId();
    m.frame = frame;
    m.name = std::move(name);
    m.color = color;
    m.comment = std::move(comment);
    m.duration = duration;
    return m;
}

void sortMarkers(std::vector<Marker>& markers) {
    std::stable_sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.frame < b.frame; });
}

Error markerNotFound() {
    return makeError(ErrorCode::NotFound, "timeline", "The marker no longer exists.", "Refresh the view and try again.");
}

}  // namespace

Result<std::string> EditorSession::addMarker(FrameIndex frame, std::string name, MarkerColor color, std::string comment,
                                             FrameIndex duration) {
    if (frame < 0 || duration < 0) {
        return makeError(ErrorCode::OutOfRange, "timeline", "Markers must be at or after the start of the timeline.");
    }
    Marker m = makeMarker(frame, std::move(name), color, std::move(comment), duration);
    const std::string id = m.id;
    UP_TRY(editTimeline("Add Marker", [&](Timeline& t) -> Status {
        t.markers.push_back(m);
        sortMarkers(t.markers);
        return Status::success();
    }));
    return id;
}

Result<std::string> EditorSession::addClipMarker(const std::string& clipId, FrameIndex timelineFrame, std::string name,
                                                 MarkerColor color, std::string comment) {
    const Clip* c = timeline().clip(clipId);
    if (!c) return clipNotFound(clipId);
    if (!c->contains(timelineFrame)) {
        return makeError(ErrorCode::OutOfRange, "timeline", "The playhead is not over the selected clip.",
                         "Move the playhead onto the clip, then add the marker.");
    }
    Marker m = makeMarker(c->toSource(timelineFrame), std::move(name), color, std::move(comment), 0);
    const std::string id = m.id;
    UP_TRY(editTimeline("Add Clip Marker", [&](Timeline& t) -> Status {
        Clip* clip = t.clip(clipId);
        clip->markers.push_back(m);
        sortMarkers(clip->markers);
        return Status::success();
    }));
    return id;
}

Status EditorSession::updateMarker(const Marker& marker) {
    if (marker.duration < 0) return makeError(ErrorCode::OutOfRange, "timeline", "A marker cannot have a negative length.");
    return editTimeline("Edit Marker", [&](Timeline& t) -> Status {
        for (auto& m : t.markers) {
            if (m.id != marker.id) continue;
            if (marker.frame < 0) return makeError(ErrorCode::OutOfRange, "timeline", "Markers must be at or after frame 0.");
            m = marker;
            sortMarkers(t.markers);
            return Status::success();
        }
        for (auto& tr : t.tracks)
            for (auto& c : tr.clips)
                for (auto& m : c.markers) {
                    if (m.id != marker.id) continue;
                    m = marker;
                    sortMarkers(c.markers);
                    return Status::success();
                }
        return markerNotFound();
    });
}

Status EditorSession::removeMarker(const std::string& markerId) {
    return editTimeline("Delete Marker", [&](Timeline& t) -> Status {
        auto erase = [&](std::vector<Marker>& list) {
            const auto before = list.size();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Marker& m) { return m.id == markerId; }),
                       list.end());
            return list.size() != before;
        };
        if (erase(t.markers)) return Status::success();
        for (auto& tr : t.tracks)
            for (auto& c : tr.clips)
                if (erase(c.markers)) return Status::success();
        return markerNotFound();
    });
}

std::vector<MarkerRef> EditorSession::markers() const {
    std::vector<MarkerRef> out;
    const Timeline& tl = timeline();
    for (const auto& m : tl.markers) out.push_back({m, {}, m.frame});
    for (const auto& tr : tl.tracks)
        for (const auto& c : tr.clips)
            for (const auto& m : c.markers)
                if (c.contains(c.toTimeline(m.frame))) out.push_back({m, c.id, c.toTimeline(m.frame)});
    std::stable_sort(out.begin(), out.end(),
                     [](const MarkerRef& a, const MarkerRef& b) { return a.timelineFrame < b.timelineFrame; });
    return out;
}

std::optional<MarkerRef> EditorSession::findMarker(const std::string& markerId) const {
    const Timeline& tl = timeline();
    for (const auto& m : tl.markers)
        if (m.id == markerId) return MarkerRef{m, {}, m.frame};
    for (const auto& tr : tl.tracks)
        for (const auto& c : tr.clips)
            for (const auto& m : c.markers)
                if (m.id == markerId) return MarkerRef{m, c.id, c.toTimeline(m.frame)};
    return std::nullopt;
}

// --- Clipboard ------------------------------------------------------------------------------

Result<Clipboard> EditorSession::captureClips(const std::vector<std::string>& clipIds) const {
    const Timeline& tl = timeline();
    std::vector<std::string> ids;
    for (const auto& id : clipIds) {
        if (!tl.clip(id)) return clipNotFound(id);
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        for (const auto& partner : tl.linkedClips(id))
            if (std::find(ids.begin(), ids.end(), partner) == ids.end()) ids.push_back(partner);
    }
    if (ids.empty()) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "No clips are selected.", "Select a clip first.");
    }
    auto indexOfTrack = [&](const Track* track) {
        const auto list = tl.tracksOfKind(track->kind);
        return static_cast<int>(std::find(list.begin(), list.end(), track) - list.begin());
    };
    FrameIndex first = std::numeric_limits<FrameIndex>::max();
    FrameIndex last = 0;
    int lowest[2] = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    for (const auto& id : ids) {
        const Clip* c = tl.clip(id);
        const Track* t = tl.trackOfClip(id);
        first = std::min(first, c->start);
        last = std::max(last, c->end());
        int& low = lowest[t->kind == TrackKind::Video ? 0 : 1];
        low = std::min(low, indexOfTrack(t));
    }
    Clipboard out;
    out.span = last - first;
    for (const auto& id : ids) {
        const Track* t = tl.trackOfClip(id);
        ClipboardItem item;
        item.clip = *tl.clip(id);
        item.clip.start -= first;
        item.kind = t->kind;
        item.trackOffset = indexOfTrack(t) - lowest[t->kind == TrackKind::Video ? 0 : 1];
        item.sourceTrackId = t->id;
        out.items.push_back(std::move(item));
    }
    return out;
}

Status EditorSession::copyClips(const std::vector<std::string>& clipIds) {
    auto captured = captureClips(clipIds);
    if (!captured.ok()) return captured.error();
    clipboard_ = std::move(captured.value());
    return Status::success();
}

Status EditorSession::cutClips(const std::vector<std::string>& clipIds) {
    auto captured = captureClips(clipIds);
    if (!captured.ok()) return captured.error();
    const Status lifted = editTimeline("Cut", [&](Timeline& t) -> Status {
        for (const auto& item : captured.value().items) UP_TRY(ops::lift(t, item.clip.id));
        return Status::success();
    });
    if (!lifted.ok()) return lifted;
    clipboard_ = std::move(captured.value());
    return Status::success();
}

Result<std::vector<std::string>> EditorSession::paste(FrameIndex at, ops::EditMode mode) {
    if (clipboard_.empty()) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "The clipboard is empty.", "Copy or cut clips first.");
    }
    return placeClipboard(clipboard_, at, mode, /*onSourceTracks=*/false, mode == ops::EditMode::Insert ? "Paste Insert" : "Paste");
}

Result<std::vector<std::string>> EditorSession::duplicateClips(const std::vector<std::string>& clipIds) {
    auto captured = captureClips(clipIds);
    if (!captured.ok()) return captured.error();
    FrameIndex first = std::numeric_limits<FrameIndex>::max();
    for (const auto& id : clipIds) first = std::min(first, timeline().clip(id)->start);
    for (const auto& item : captured.value().items) first = std::min(first, timeline().clip(item.clip.id)->start);
    return placeClipboard(captured.value(), first + captured.value().span, ops::EditMode::Overwrite,
                          /*onSourceTracks=*/true, "Duplicate");
}

Result<std::vector<std::string>> EditorSession::placeClipboard(const Clipboard& content, FrameIndex at,
                                                               ops::EditMode mode, bool onSourceTracks,
                                                               const std::string& name) {
    if (at < 0) return makeError(ErrorCode::OutOfRange, "timeline", "Cannot paste before the start of the timeline.");
    for (const auto& item : content.items) {
        if (!project_.findMedia(item.clip.mediaId)) {
            return makeError(ErrorCode::NotFound, "timeline",
                             "'" + item.clip.name + "' refers to media that is not in this project.",
                             "Import the media into this project first.");
        }
    }
    std::vector<std::string> placed;
    const Status status = editTimeline(name, [&](Timeline& t) -> Status {
        placed.clear();
        // Resolve destination tracks before anything moves.
        std::vector<std::string> destinations;
        for (const auto& item : content.items) {
            if (onSourceTracks) {
                destinations.push_back(item.sourceTrackId);
                continue;
            }
            const std::string& target = item.kind == TrackKind::Video ? t.videoTarget : t.audioTarget;
            if (target.empty()) {
                destinations.emplace_back();  // this kind is not patched: skip
                continue;
            }
            const auto ids = t.trackIdsOfKind(item.kind);
            const auto base = std::find(ids.begin(), ids.end(), target) - ids.begin();
            const auto index = static_cast<std::size_t>(base + item.trackOffset);
            if (index >= ids.size()) {
                return makeError(ErrorCode::OutOfRange, "timeline",
                                 "There are not enough " + std::string(toString(item.kind)) +
                                     " tracks above the target to paste these clips.",
                                 "Target a lower track or add tracks.");
            }
            destinations.push_back(ids[index]);
        }
        if (std::all_of(destinations.begin(), destinations.end(), [](const std::string& d) { return d.empty(); })) {
            return makeError(ErrorCode::InvalidArgument, "timeline", "No target track is enabled for the copied clips.",
                             "Click a track's target box in the timeline header.");
        }
        if (mode == ops::EditMode::Insert) {
            for (const auto& track : t.tracks)
                if (!track.locked) UP_TRY(ops::insertGap(t, track.id, at, content.span));
        }
        std::map<std::string, std::string> links;  // copied link id -> fresh link id
        for (std::size_t i = 0; i < content.items.size(); ++i) {
            if (destinations[i].empty()) continue;
            Clip clip = content.items[i].clip;
            clip.id.clear();
            clip.start += at;
            if (!clip.linkId.empty()) {
                auto [it, inserted] = links.try_emplace(clip.linkId, generateId());
                clip.linkId = it->second;
            }
            for (auto& m : clip.markers) m.id = generateId();
            auto id = ops::placeClip(t, destinations[i], std::move(clip), ops::EditMode::Overwrite);
            if (!id.ok()) return id.error();
            placed.push_back(id.value());
        }
        // A pasted clip whose partner was skipped (disabled target) is no longer linked.
        for (const auto& id : placed) {
            Clip* c = t.clip(id);
            if (!c->linkId.empty() && t.linkedClips(id).empty()) c->linkId.clear();
        }
        return Status::success();
    });
    if (!status.ok()) return status.error();
    return placed;
}

// --- Tracks -------------------------------------------------------------------------------

namespace {

Error trackNotFound() {
    return makeError(ErrorCode::NotFound, "timeline", "The track does not exist.", "Refresh the timeline view and try again.");
}

bool nameTaken(const Timeline& t, const std::string& name, const std::string& exceptId) {
    return std::any_of(t.tracks.begin(), t.tracks.end(),
                       [&](const Track& tr) { return tr.name == name && tr.id != exceptId; });
}

}  // namespace

Result<std::string> EditorSession::addTrack(TrackKind kind, std::string name) {
    std::string id;
    const Status s = editTimeline("Add Track", [&](Timeline& t) -> Status {
        if (!name.empty() && nameTaken(t, name, {})) {
            return makeError(ErrorCode::Conflict, "timeline", "A track named '" + name + "' already exists.",
                             "Choose a different name.");
        }
        Track& track = t.addTrack(kind);
        if (!name.empty()) track.name = name;
        id = track.id;
        // A timeline that had no target for this kind starts targeting the new track.
        std::string& target = kind == TrackKind::Video ? t.videoTarget : t.audioTarget;
        if (target.empty() && t.trackIdsOfKind(kind).size() == 1) target = id;
        return Status::success();
    });
    if (!s.ok()) return s.error();
    return id;
}

Status EditorSession::removeTrack(const std::string& trackId, bool evenIfNotEmpty) {
    return editTimeline("Delete Track", [&](Timeline& t) -> Status {
        const Track* track = t.track(trackId);
        if (!track) return trackNotFound();
        if (track->locked) {
            return makeError(ErrorCode::Locked, "timeline", "Track " + track->name + " is locked.", "Unlock it before deleting it.");
        }
        if (t.trackIdsOfKind(track->kind).size() <= 1) {
            return makeError(ErrorCode::InvalidArgument, "timeline",
                             "A timeline needs at least one " + std::string(toString(track->kind)) + " track.",
                             "Add another track before deleting this one.");
        }
        if (!track->clips.empty() && !evenIfNotEmpty) {
            return makeError(ErrorCode::Conflict, "timeline",
                             "Track " + track->name + " contains " + std::to_string(track->clips.size()) + " clip(s).",
                             "Confirm the deletion to remove the track together with its clips.");
        }
        const TrackKind kind = track->kind;
        // Linked partners of removed clips become unlinked.
        std::vector<std::string> links;
        for (const auto& c : track->clips)
            if (!c.linkId.empty()) links.push_back(c.linkId);
        t.tracks.erase(std::find_if(t.tracks.begin(), t.tracks.end(), [&](const Track& tr) { return tr.id == trackId; }));
        for (auto& tr : t.tracks)
            for (auto& c : tr.clips)
                if (std::find(links.begin(), links.end(), c.linkId) != links.end() && t.linkedClips(c.id).empty())
                    c.linkId.clear();
        std::string& target = kind == TrackKind::Video ? t.videoTarget : t.audioTarget;
        if (target == trackId) target = t.trackIdsOfKind(kind).front();
        return Status::success();
    });
}

Status EditorSession::renameTrack(const std::string& trackId, const std::string& name) {
    return editTimeline("Rename Track", [&](Timeline& t) -> Status {
        Track* track = t.track(trackId);
        if (!track) return trackNotFound();
        if (name.empty()) return makeError(ErrorCode::InvalidArgument, "timeline", "A track name cannot be empty.");
        if (nameTaken(t, name, trackId)) {
            return makeError(ErrorCode::Conflict, "timeline", "A track named '" + name + "' already exists.",
                             "Choose a different name.");
        }
        track->name = name;
        return Status::success();
    });
}

Status EditorSession::moveTrack(const std::string& trackId, int index) {
    return editTimeline("Move Track", [&](Timeline& t) -> Status {
        const Track* track = t.track(trackId);
        if (!track) return trackNotFound();
        const TrackKind kind = track->kind;
        // Reorder the tracks of this kind within the slots they already occupy.
        std::vector<std::size_t> slots;
        std::vector<Track> ofKind;
        for (std::size_t i = 0; i < t.tracks.size(); ++i) {
            if (t.tracks[i].kind != kind) continue;
            slots.push_back(i);
            ofKind.push_back(t.tracks[i]);
        }
        if (index < 0 || index >= static_cast<int>(ofKind.size())) {
            return makeError(ErrorCode::OutOfRange, "timeline", "There is no track position " + std::to_string(index + 1) + ".");
        }
        auto it = std::find_if(ofKind.begin(), ofKind.end(), [&](const Track& tr) { return tr.id == trackId; });
        Track moving = *it;
        ofKind.erase(it);
        ofKind.insert(ofKind.begin() + index, std::move(moving));
        for (std::size_t i = 0; i < slots.size(); ++i) t.tracks[slots[i]] = std::move(ofKind[i]);
        return Status::success();
    });
}

// --- Clip transforms --------------------------------------------------------------------

namespace {

// Finds a video clip for a transform edit and, when `frame` is given, checks it lies inside.
Result<Clip*> transformTarget(Timeline& t, const std::string& clipId, std::optional<FrameIndex> frame) {
    Clip* c = t.clip(clipId);
    if (!c) return clipNotFound(clipId);
    const Track* track = t.trackOfClip(clipId);
    if (track->kind != TrackKind::Video) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "Transforms apply to video clips only.",
                         "Select the clip on the video track.");
    }
    if (track->locked) return makeError(ErrorCode::Locked, "timeline", "Track " + track->name + " is locked.", "Unlock it first.");
    if (frame && !c->contains(*frame)) {
        return makeError(ErrorCode::OutOfRange, "timeline", "The playhead is not over the clip.",
                         "Move the playhead onto the clip to set a keyframe.");
    }
    return c;
}

double clampParam(ClipParam p, double v) {
    const ClipParamInfo& info = paramInfo(p);
    return std::clamp(v, info.minimum, info.maximum);
}

}  // namespace

Status EditorSession::setClipParameter(const std::string& clipId, ClipParam param, double value, FrameIndex timelineFrame) {
    if (!std::isfinite(value)) return makeError(ErrorCode::InvalidArgument, "timeline", "The value is not a number.");
    return editTimeline(std::string("Set ") + paramInfo(param).label, [&](Timeline& t) -> Status {
        const bool animated = t.clip(clipId) && t.clip(clipId)->transform[param].animated();
        auto c = transformTarget(t, clipId, animated ? std::optional<FrameIndex>(timelineFrame) : std::nullopt);
        if (!c.ok()) return c.error();
        AnimatedValue& v = c.value()->transform[param];
        if (v.animated()) v.setKey(c.value()->toSource(timelineFrame), clampParam(param, value));
        else v.value = clampParam(param, value);
        return Status::success();
    });
}

Status EditorSession::setKeyframe(const std::string& clipId, ClipParam param, FrameIndex timelineFrame, bool present) {
    return editTimeline(present ? "Add Keyframe" : "Remove Keyframe", [&](Timeline& t) -> Status {
        auto c = transformTarget(t, clipId, timelineFrame);
        if (!c.ok()) return c.error();
        AnimatedValue& v = c.value()->transform[param];
        const FrameIndex source = c.value()->toSource(timelineFrame);
        if (present) {
            v.setKey(source, v.at(source));
            return Status::success();
        }
        const Keyframe* key = v.keyAt(source);
        if (!key) return makeError(ErrorCode::NotFound, "timeline", "There is no keyframe at the playhead.");
        const double kept = key->value;
        v.removeKey(source);
        if (!v.animated()) v.value = kept;
        return Status::success();
    });
}

Status EditorSession::setKeyframeInterpolation(const std::string& clipId, ClipParam param, FrameIndex timelineFrame,
                                               Interpolation interpolation) {
    return editTimeline("Keyframe Interpolation", [&](Timeline& t) -> Status {
        auto c = transformTarget(t, clipId, timelineFrame);
        if (!c.ok()) return c.error();
        AnimatedValue& v = c.value()->transform[param];
        const FrameIndex source = c.value()->toSource(timelineFrame);
        const Keyframe* key = v.keyAt(source);
        if (!key) return makeError(ErrorCode::NotFound, "timeline", "There is no keyframe at the playhead.");
        v.setKey(source, key->value, interpolation);
        return Status::success();
    });
}

Status EditorSession::resetClipParameter(const std::string& clipId, ClipParam param) {
    return editTimeline(std::string("Reset ") + paramInfo(param).label, [&](Timeline& t) -> Status {
        auto c = transformTarget(t, clipId, std::nullopt);
        if (!c.ok()) return c.error();
        c.value()->transform[param] = AnimatedValue{paramInfo(param).defaultValue, {}};
        return Status::success();
    });
}

// --- Transitions ------------------------------------------------------------------------

namespace {

// The clip plus linked partners (on unlocked tracks) whose `edge` lines up with it.
std::vector<std::string> edgeGroup(const Timeline& t, const std::string& clipId, ops::Edge edge, bool withLinked) {
    std::vector<std::string> out{clipId};
    if (!withLinked) return out;
    const Clip* c = t.clip(clipId);
    const FrameIndex at = edge == ops::Edge::In ? c->start : c->end();
    for (const auto& id : editableGroup(t, clipId)) {
        if (id == clipId) continue;
        const Clip* p = t.clip(id);
        if ((edge == ops::Edge::In ? p->start : p->end()) == at) out.push_back(id);
    }
    return out;
}

FrameIndex groupMaxDuration(const Timeline& t, const std::vector<std::string>& ids, ops::Edge edge,
                            TransitionAlignment alignment) {
    FrameIndex most = std::numeric_limits<FrameIndex>::max();
    for (const auto& id : ids)
        most = std::min(most, transitions::maxDuration(*t.trackOfClip(id), *t.clip(id), edge == ops::Edge::In, alignment));
    return most;
}

}  // namespace

Status EditorSession::setTransition(const std::string& clipId, ops::Edge edge, std::optional<Transition> transition,
                                    bool withLinked) {
    if (transition && transition->duration <= 0) {
        return makeError(ErrorCode::InvalidArgument, "timeline", "A transition must be at least one frame long.");
    }
    const char* name = !transition ? "Remove Transition" : edge == ops::Edge::In ? "Add Transition In" : "Add Transition Out";
    return editTimeline(name, [&](Timeline& t) -> Status {
        if (!t.clip(clipId)) return clipNotFound(clipId);
        const Track* own = t.trackOfClip(clipId);
        if (own->locked) return makeError(ErrorCode::Locked, "timeline", "Track " + own->name + " is locked.", "Unlock it first.");
        const auto ids = edgeGroup(t, clipId, edge, withLinked);
        if (transition) {
            const FrameIndex most = groupMaxDuration(t, ids, edge, transition->alignment);
            if (transition->duration > most) {
                return makeError(ErrorCode::OutOfRange, "timeline",
                                 "There is not enough media for a " + std::to_string(transition->duration) +
                                     "-frame transition here; at most " + std::to_string(most) + " frame(s) fit.",
                                 "Use a shorter transition, or trim the clips so unused media (handles) remains beyond the cut.");
            }
        }
        for (const auto& id : ids) {
            Clip* c = t.clip(id);
            (edge == ops::Edge::In ? c->transitionIn : c->transitionOut) = transition;
        }
        return Status::success();
    });
}

Result<FrameIndex> EditorSession::applyDefaultTransition(const std::string& clipId, FrameIndex frame,
                                                         TransitionKind kind, FrameIndex preferred) {
    const Timeline& tl = timeline();
    const Clip* c = tl.clip(clipId);
    if (!c) return clipNotFound(clipId);
    if (preferred <= 0) preferred = std::max<FrameIndex>(2, nominalFps(tl.frameRate));  // one second
    const ops::Edge edge = std::abs(frame - c->start) <= std::abs(c->end() - frame) ? ops::Edge::In : ops::Edge::Out;
    const FrameIndex most = groupMaxDuration(tl, edgeGroup(tl, clipId, edge, true), edge, TransitionAlignment::Center);
    const FrameIndex length = std::min(preferred, most);
    if (length < 2) {
        return makeError(ErrorCode::OutOfRange, "timeline", "There is not enough media at this edit for a transition.",
                         "Trim the clips so unused media (handles) remains beyond the cut.");
    }
    UP_TRY(setTransition(clipId, edge, Transition{kind, length, TransitionAlignment::Center}, true));
    return length;
}

}  // namespace up

