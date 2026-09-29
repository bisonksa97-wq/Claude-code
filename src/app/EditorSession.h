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
                                                FrameIndex sourceIn = 0, FrameIndex duration = 0);
    Result<std::vector<std::string>> appendMedia(const std::string& mediaId);

    // Cuts every clip under `frame` on unlocked tracks. Returns number of clips cut.
    Result<int> razorAt(FrameIndex frame);
    Status liftClip(const std::string& clipId);
    Status rippleDeleteClip(const std::string& clipId);
    Status trimClip(const std::string& clipId, ops::Edge edge, FrameIndex delta, ops::TrimMode mode);
    Status rollEdit(const std::string& leftClipId, FrameIndex delta);
    Status slipClip(const std::string& clipId, FrameIndex delta);
    Status slideClip(const std::string& clipId, FrameIndex delta);
    // Moves a clip (and its linked partners by the same offset) to `trackId` at `newStart`.
    Status moveClip(const std::string& clipId, const std::string& trackId, FrameIndex newStart);
    Status setTrackState(const std::string& trackId, const TrackState& state);

    bool undo() { return history_.undo(); }
    bool redo() { return history_.redo(); }

    void setChangeListener(ChangeListener listener) { listener_ = std::move(listener); }

private:
    explicit EditorSession(Project project);
    Status editTimeline(const std::string& name, std::function<Status(Timeline&)> edit);

    Project project_;
    CommandStack history_;
    ChangeListener listener_;
};

}  // namespace up
