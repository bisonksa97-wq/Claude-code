#include "media/MediaLibrary.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "codec/MediaProbe.h"
#include "core/Id.h"
#include "core/Log.h"

namespace up::media {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

Result<MediaItem> createMediaItem(const fs::path& path, const std::string& binId) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    auto info = probeMedia(absolute);
    if (!info.ok()) return info.error();
    MediaItem item;
    item.id = generateId();
    item.name = absolute.filename().string();
    item.path = absolute.lexically_normal();
    item.binId = binId;
    item.info = info.value();
    item.importedAt = currentUtcTimestamp();
    item.online = true;
    UP_LOG_INFO(log::sub::Media, "Imported " << item.path.string());
    return item;
}

std::size_t refreshOnlineState(Project& project) {
    std::size_t offline = 0;
    const fs::path base = project.filePath.empty() ? fs::path() : fs::absolute(project.filePath).parent_path();
    for (auto& m : project.media) {
        std::error_code ec;
        if (!m.path.empty() && fs::is_regular_file(m.path, ec)) {
            m.online = true;
            continue;
        }
        if (!base.empty() && !m.relativePath.empty()) {
            const fs::path candidate = (base / m.relativePath).lexically_normal();
            if (fs::is_regular_file(candidate, ec)) {
                UP_LOG_INFO(log::sub::Media, "Found moved media '" << m.name << "' at " << candidate.string());
                m.path = candidate;
                m.online = true;
                continue;
            }
        }
        m.online = false;
        ++offline;
        UP_LOG_WARN(log::sub::Media, "Media offline: " << m.name << " (" << m.path.string() << ")");
    }
    return offline;
}

Status checkRelinkCandidate(const MediaItem& item, const fs::path& newPath, MediaInfo* probed) {
    auto info = probeMedia(newPath);
    if (!info.ok()) return info.error();
    const MediaInfo& n = info.value();
    const MediaInfo& o = item.info;
    if (o.hasVideo && !n.hasVideo) {
        return makeError(ErrorCode::Conflict, "media",
                         "'" + newPath.filename().string() + "' has no video, but '" + item.name + "' did.",
                         "Choose the original file or a copy of it.");
    }
    if (o.hasAudio && !n.hasAudio) {
        return makeError(ErrorCode::Conflict, "media",
                         "'" + newPath.filename().string() + "' has no audio, but '" + item.name + "' did.",
                         "Choose the original file or a copy of it.");
    }
    // Allow a small tolerance: containers report durations with differing precision.
    if (!o.isStill && n.durationSeconds + 0.1 < o.durationSeconds) {
        return makeError(ErrorCode::Conflict, "media",
                         "'" + newPath.filename().string() + "' is shorter than '" + item.name +
                             "', so clips using it could reference missing frames.",
                         "Choose the original file or a copy of it.",
                         "original " + std::to_string(o.durationSeconds) + "s, candidate " +
                             std::to_string(n.durationSeconds) + "s");
    }
    if (probed) *probed = n;
    return Status::success();
}

FrameIndex lengthInFrames(const MediaInfo& info, FrameRate rate) {
    if (info.hasVideo && info.isStill) return 0;
    return static_cast<FrameIndex>(std::floor(info.durationSeconds * rate.toDouble() + 1e-6));
}

std::vector<const MediaItem*> search(const Project& project, const std::string& query) {
    std::vector<const MediaItem*> out;
    const std::string q = lower(query);
    for (const auto& m : project.media) {
        if (q.empty()) {
            out.push_back(&m);
            continue;
        }
        std::string haystack = m.name + " " + m.path.string() + " " + m.comment + " " + m.info.videoCodec + " " +
                               m.info.audioCodec + " " + m.info.container;
        for (const auto& k : m.keywords) haystack += " " + k;
        if (lower(haystack).find(q) != std::string::npos) out.push_back(&m);
    }
    return out;
}

}  // namespace up::media
