#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/MediaInfo.h"
#include "core/Rational.h"
#include "timeline/Timeline.h"

namespace up {

// A media file referenced by the project. Media is never embedded in the project file.
struct MediaItem {
    std::string id;
    std::string name;
    // Last known absolute location, and the location relative to the project file
    // (used to find media when a project folder is moved as a whole).
    std::filesystem::path path;
    std::filesystem::path relativePath;
    std::string binId;
    MediaInfo info;
    int rating = 0;  // 0-5
    std::vector<std::string> keywords;
    std::string comment;
    std::string importedAt;  // ISO-8601 UTC
    // Source in/out marks in seconds from the start of the media ([in, out)).
    std::optional<double> markIn;
    std::optional<double> markOut;

    // Runtime state, not persisted: whether the file is currently reachable.
    bool online = true;
};

struct Bin {
    std::string id;
    std::string name;
    std::string parentId;  // empty = root
};

// Defaults used for new timelines.
struct SequenceSettings {
    FrameRate frameRate{25, 1};
    int width = 1920;
    int height = 1080;
    int sampleRate = 48000;
};

class Project {
public:
    static constexpr int kFormatVersion = 8;

    std::string id;
    std::string name;
    std::string createdAt;
    std::string modifiedAt;
    SequenceSettings settings;
    std::vector<Bin> bins;
    std::vector<MediaItem> media;
    std::vector<Timeline> timelines;
    std::string activeTimelineId;

    // Location of the .uproj file; empty until first saved. Not persisted.
    std::filesystem::path filePath;

    static Project create(std::string name, SequenceSettings settings = {});

    MediaItem* findMedia(const std::string& mediaId);
    const MediaItem* findMedia(const std::string& mediaId) const;
    Timeline* findTimeline(const std::string& timelineId);
    const Timeline* findTimeline(const std::string& timelineId) const;
    Timeline* activeTimeline();
    const Timeline* activeTimeline() const;
};

std::string currentUtcTimestamp();

}  // namespace up
