#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/CommandStack.h"
#include "core/Result.h"
#include "project/Project.h"
#include "timeline/EditOperations.h"

namespace up {

struct TrackState {
    bool enabled = true;
    bool locked = false;
    bool muted = false;
    bool solo = false;
    double gainDb = 0.0;
};

// A marker together with where it lives and where it currently appears.
struct MarkerRef {
    Marker marker;
    std::string clipId;  // empty for timeline markers
    FrameIndex timelineFrame = 0;
};

// Copied clips, positioned relative to the earliest copied clip.
struct ClipboardItem {
    Clip clip;               // clip.start is relative to the copied range start
    TrackKind kind = TrackKind::Video;
    int trackOffset = 0;     // tracks above the lowest copied track of this kind
    std::string sourceTrackId;
};

struct Clipboard {
    std::vector<ClipboardItem> items;
    FrameIndex span = 0;  // length of the copied range
    bool empty() const { return items.empty(); }
};

struct ImportReport {
    std::vector<std::string> importedIds;
    std::vector<Error> failures;
};

// Application service layer: the single entry point the UI, CLI and future
// automation/scripting use to change a project. Every mutation is an undoable
// command; linked clips (video + audio of one file) are edited together.
class EditorSession {
public:
    using ChangeListener = std::function<void()>;

    static std::unique_ptr<EditorSession> createNew(std::string name, SequenceSettings settings = {});
    static Result<std::unique_ptr<EditorSession>> open(const std::filesystem::path& path);
    // Opens an autosave/recovery file. `originalPath` becomes the save location (may be empty).
    static Result<std::unique_ptr<EditorSession>> openRecovery(const std::filesystem::path& recoveryFile,
                                                               const std::filesystem::path& originalPath);

    Project& project() { return project_; }
    const Project& project() const { return project_; }
    CommandStack& history() { return history_; }
    Timeline& timeline();
    const Timeline& timeline() const;

    bool isDirty() const { return !history_.isClean(); }
    Status save();
    Status saveAs(const std::filesystem::path& path);

    // --- Crash recovery -------------------------------------------------------
    std::filesystem::path autosavePath() const;
    Status writeAutosave();
    void discardAutosave();
    // An autosave next to `projectFile` that is newer than it, if any.
    static std::optional<std::filesystem::path> newerAutosaveFor(const std::filesystem::path& projectFile);
    static std::filesystem::path recoveryDirectory();

    // --- Media ------------------------------------------------------------------
    ImportReport importMedia(const std::vector<std::filesystem::path>& paths);
    std::size_t refreshMediaStatus();
    Status relinkMedia(const std::string& mediaId, const std::filesystem::path& newPath);

    // --- Timeline editing ------------------------------------------------------
    // Places a media item at `at`: video on `videoTrackId` (or the first unlocked video
    // track) and audio on `audioTrackId` (or the first unlocked audio track), linked.
    // Insert mode opens a gap on every unlocked track so they stay in sync.
    // `sourceIn`/`duration` default to the whole media (stills default to 5 seconds).
    Result<std::vector<std::string>> placeMedia(const std::string& mediaId, FrameIndex at, ops::EditMode mode,
                                                std::string videoTrackId = {}, std::string audioTrackId = {},
                                                FrameIndex sourceIn = 0, FrameIndex duration = 0,
                                                bool useVideo = true, bool useAudio = true);
    Result<std::vector<std::string>> appendMedia(const std::string& mediaId);

    // Cuts every clip under `frame` on unlocked tracks. Returns number of clips cut.
    Result<int> razorAt(FrameIndex frame);
    Status liftClip(const std::string& clipId);
    Status rippleDeleteClip(const std::string& clipId);
    // Multi-clip versions (linked partners on unlocked tracks included; one undo step).
    Status liftClips(const std::vector<std::string>& clipIds);
    // Removes the clips and closes each gap on the clip's own track.
    Status rippleDeleteClips(const std::vector<std::string>& clipIds);
    // Moves clips (and partners) by `delta` frames; clips of `trackShiftKind` also move
    // `trackShift` tracks up (+) or down (-) within their kind. The moved clips never
    // overwrite each other; they overwrite anything else at their destinations.
    Status moveClips(const std::vector<std::string>& clipIds, FrameIndex delta, int trackShift = 0,
                     TrackKind trackShiftKind = TrackKind::Video);
    // Clips starting at or after `frame` on unlocked tracks (for "select forward").
    std::vector<std::string> clipsFrom(FrameIndex frame) const;
    Status trimClip(const std::string& clipId, ops::Edge edge, FrameIndex delta, ops::TrimMode mode);
    Status rollEdit(const std::string& leftClipId, FrameIndex delta);
    Status slipClip(const std::string& clipId, FrameIndex delta);
    Status slideClip(const std::string& clipId, FrameIndex delta);
    // Moves a clip (and its linked partners by the same offset) to `trackId` at `newStart`.
    Status moveClip(const std::string& clipId, const std::string& trackId, FrameIndex newStart);
    Status setTrackState(const std::string& trackId, const TrackState& state);

    // --- Tracks ------------------------------------------------------------------------
    // Adds a track above the others of its kind. Returns its id.
    Result<std::string> addTrack(TrackKind kind, std::string name = {});
    // Removes a track. A track holding clips is only removed with `evenIfNotEmpty`; the
    // last track of a kind cannot be removed. Targets on it move to the first remaining track.
    Status removeTrack(const std::string& trackId, bool evenIfNotEmpty = false);
    // Names must be non-empty and unique in the timeline.
    Status renameTrack(const std::string& trackId, const std::string& name);
    // Moves a track to `index` among the tracks of its kind (0 = V1/A1).
    Status moveTrack(const std::string& trackId, int index);

    // --- Three-point editing ------------------------------------------------------
    // Source marks, in timeline frames from the start of the media (nullopt clears a mark).
    Status setMediaMarks(const std::string& mediaId, std::optional<FrameIndex> in, std::optional<FrameIndex> out);
    // Source marks of a media item converted to timeline frames.
    std::pair<std::optional<FrameIndex>, std::optional<FrameIndex>> mediaMarks(const std::string& mediaId) const;
    Status setTimelineMarks(std::optional<FrameIndex> in, std::optional<FrameIndex> out);
    // Source patching; an empty id disables that stream for three-point edits.
    Status setTrackTargets(const std::string& videoTrackId, const std::string& audioTrackId);

    struct EditResult {
        std::vector<std::string> clipIds;
        FrameIndex recordIn = 0;
        FrameIndex recordOut = 0;
    };
    // Insert/overwrite `mediaId` using its source marks, the timeline marks (or the
    // playhead) and the track targets; see ops::resolveThreePointEdit. Clears the
    // timeline marks afterwards. One undo step.
    Result<EditResult> threePointEdit(const std::string& mediaId, ops::EditMode mode, FrameIndex playhead);

    // --- Markers ----------------------------------------------------------------------
    Result<std::string> addMarker(FrameIndex frame, std::string name = {}, MarkerColor color = MarkerColor::Blue,
                                  std::string comment = {}, FrameIndex duration = 0);
    // Adds a marker to a clip at a timeline frame inside it; it is stored in source frames.
    Result<std::string> addClipMarker(const std::string& clipId, FrameIndex timelineFrame, std::string name = {},
                                      MarkerColor color = MarkerColor::Red, std::string comment = {});
    // Replaces name, comment, colour and duration (and, for timeline markers, the frame).
    Status updateMarker(const Marker& marker);
    Status removeMarker(const std::string& markerId);
    std::optional<MarkerRef> findMarker(const std::string& markerId) const;
    // Every marker in timeline order (clip markers only while visible in their clip).
    std::vector<MarkerRef> markers() const;

    // --- Clipboard ------------------------------------------------------------------
    // Copies clips and their linked partners (not undoable; does not change the project).
    Status copyClips(const std::vector<std::string>& clipIds);
    // Copy, then lift the clips (one undo step).
    Status cutClips(const std::vector<std::string>& clipIds);
    // Pastes at `at` on the targeted tracks: each kind's lowest copied track lands on
    // that kind's target, the others keep their offset. A disabled target skips that
    // kind. Insert opens a gap on every unlocked track first. Returns the new clip ids.
    Result<std::vector<std::string>> paste(FrameIndex at, ops::EditMode mode);
    // Copies the clips (with linked partners) onto their own tracks, directly after the
    // last of them, overwriting whatever is there. Does not touch the clipboard.
    Result<std::vector<std::string>> duplicateClips(const std::vector<std::string>& clipIds);
    const Clipboard& clipboard() const { return clipboard_; }

    bool undo() { return history_.undo(); }
    bool redo() { return history_.redo(); }

    void setChangeListener(ChangeListener listener) { listener_ = std::move(listener); }

private:
    explicit EditorSession(Project project);
    Status editTimeline(const std::string& name, std::function<Status(Timeline&)> edit);

    Result<Clipboard> captureClips(const std::vector<std::string>& clipIds) const;
    Result<std::vector<std::string>> placeClipboard(const Clipboard& content, FrameIndex at, ops::EditMode mode,
                                                     bool onSourceTracks, const std::string& name);

    Project project_;
    CommandStack history_;
    Clipboard clipboard_;
    ChangeListener listener_;
};

}  // namespace up
