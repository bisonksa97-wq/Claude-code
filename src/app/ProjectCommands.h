#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Command.h"
#include "project/Project.h"

namespace up {

// Applies an arbitrary edit to one timeline and records before/after snapshots
// (memento). Undo restores the exact previous timeline; redo restores the exact
// result, so redo is deterministic even when the edit generated new ids.
class TimelineEditCommand final : public Command {
public:
    using Edit = std::function<Status(Timeline&)>;

    TimelineEditCommand(Project& project, std::string timelineId, std::string name, Edit edit);

    std::string name() const override { return name_; }
    Status apply() override;
    void revert() override;

private:
    Project& project_;
    std::string timelineId_;
    std::string name_;
    Edit edit_;
    std::optional<Timeline> before_;
    std::optional<Timeline> after_;
};

class AddMediaCommand final : public Command {
public:
    AddMediaCommand(Project& project, std::vector<MediaItem> items);

    std::string name() const override;
    Status apply() override;
    void revert() override;

private:
    Project& project_;
    std::vector<MediaItem> items_;
};

// Replaces a media item's editable metadata (marks, rating, keywords...) with `updated`.
class UpdateMediaCommand final : public Command {
public:
    UpdateMediaCommand(Project& project, MediaItem updated, std::string name)
        : project_(project), updated_(std::move(updated)), name_(std::move(name)) {}

    std::string name() const override { return name_; }
    Status apply() override;
    void revert() override;

private:
    Project& project_;
    MediaItem updated_;
    std::optional<MediaItem> before_;
    std::string name_;
};

// Replaces a media item's file reference (relink / replace source).
class RelinkMediaCommand final : public Command {
public:
    RelinkMediaCommand(Project& project, std::string mediaId, std::filesystem::path newPath, MediaInfo newInfo);

    std::string name() const override { return "Relink Media"; }
    Status apply() override;
    void revert() override;

private:
    Project& project_;
    std::string mediaId_;
    std::filesystem::path newPath_;
    MediaInfo newInfo_;
    std::filesystem::path oldPath_;
    MediaInfo oldInfo_;
    bool oldOnline_ = false;
};

}  // namespace up
