#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Result.h"
#include "project/Project.h"

namespace up {

// Media management services operating on a project's media pool.
namespace media {

// Probes `path` and builds a MediaItem ready to be added to a project.
Result<MediaItem> createMediaItem(const std::filesystem::path& path, const std::string& binId);

// Resolves each item's file (absolute path, then path relative to the project file)
// and updates `online`. Returns the number of offline items.
std::size_t refreshOnlineState(Project& project);

// Checks that `newPath` is a plausible replacement for `item`: it must exist, be
// probe-able, provide the same kinds of streams and not be shorter than the original.
Status checkRelinkCandidate(const MediaItem& item, const std::filesystem::path& newPath, MediaInfo* probed = nullptr);

// Length of the media in timeline frames at `rate` (0 for stills = unbounded).
FrameIndex lengthInFrames(const MediaInfo& info, FrameRate rate);

// Case-insensitive search over name, path, keywords, comment and codec names.
std::vector<const MediaItem*> search(const Project& project, const std::string& query);

}  // namespace media
}  // namespace up
