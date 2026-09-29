#include "app/ProjectCommands.h"

#include <algorithm>

namespace up {

TimelineEditCommand::TimelineEditCommand(Project& project, std::string timelineId, std::string name, Edit edit)
    : project_(project), timelineId_(std::move(timelineId)), name_(std::move(name)), edit_(std::move(edit)) {}

Status TimelineEditCommand::apply() {
    Timeline* tl = project_.findTimeline(timelineId_);
    if (!tl) {
        return makeError(ErrorCode::NotFound, "timeline", "The timeline for '" + name_ + "' no longer exists.");
    }
    if (after_) {
        *tl = *after_;
        return Status::success();
    }
    before_ = *tl;
    Status s = edit_(*tl);
    if (s.ok()) s = tl->validate();
    if (!s.ok()) {
        *tl = *before_;
        before_.reset();
        return s;
    }
    after_ = *tl;
    return s;
}

void TimelineEditCommand::revert() {
    if (Timeline* tl = project_.findTimeline(timelineId_); tl && before_) *tl = *before_;
}

AddMediaCommand::AddMediaCommand(Project& project, std::vector<MediaItem> items)
    : project_(project), items_(std::move(items)) {}

std::string AddMediaCommand::name() const {
    return items_.size() == 1 ? "Import '" + items_.front().name + "'" : "Import " + std::to_string(items_.size()) + " Files";
}

Status AddMediaCommand::apply() {
    for (const auto& item : items_) project_.media.push_back(item);
    return Status::success();
}

void AddMediaCommand::revert() {
    auto& media = project_.media;
    media.erase(std::remove_if(media.begin(), media.end(),
                               [&](const MediaItem& m) {
                                   return std::any_of(items_.begin(), items_.end(),
                                                      [&](const MediaItem& i) { return i.id == m.id; });
                               }),
                media.end());
}

Status UpdateMediaCommand::apply() {
    MediaItem* m = project_.findMedia(updated_.id);
    if (!m) return makeError(ErrorCode::NotFound, "media", "The media item no longer exists.");
    before_ = *m;
    MediaItem next = updated_;
    next.online = m->online;  // runtime state is not part of the edit
    *m = std::move(next);
    return Status::success();
}

void UpdateMediaCommand::revert() {
    if (MediaItem* m = project_.findMedia(updated_.id); m && before_) {
        const bool online = m->online;
        *m = *before_;
        m->online = online;
    }
}

RelinkMediaCommand::RelinkMediaCommand(Project& project, std::string mediaId, std::filesystem::path newPath,
                                       MediaInfo newInfo)
    : project_(project), mediaId_(std::move(mediaId)), newPath_(std::move(newPath)), newInfo_(std::move(newInfo)) {}

Status RelinkMediaCommand::apply() {
    MediaItem* m = project_.findMedia(mediaId_);
    if (!m) return makeError(ErrorCode::NotFound, "media", "The media item to relink no longer exists.");
    oldPath_ = m->path;
    oldInfo_ = m->info;
    oldOnline_ = m->online;
    m->path = newPath_;
    m->relativePath.clear();
    m->info = newInfo_;
    m->online = true;
    return Status::success();
}

void RelinkMediaCommand::revert() {
    if (MediaItem* m = project_.findMedia(mediaId_)) {
        m->path = oldPath_;
        m->info = oldInfo_;
        m->online = oldOnline_;
    }
}

}  // namespace up
