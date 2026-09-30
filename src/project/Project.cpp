#include "project/Project.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "core/Id.h"

namespace up {

std::string currentUtcTimestamp() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

Project Project::create(std::string name, SequenceSettings settings) {
    Project p;
    p.id = generateId();
    p.name = std::move(name);
    p.createdAt = currentUtcTimestamp();
    p.modifiedAt = p.createdAt;
    p.settings = settings;
    p.bins.push_back(Bin{generateId(), "Master", {}});
    Timeline tl = Timeline::create("Timeline 1", settings.frameRate, settings.width, settings.height,
                                   settings.sampleRate);
    p.activeTimelineId = tl.id;
    p.timelines.push_back(std::move(tl));
    return p;
}

MediaItem* Project::findMedia(const std::string& mediaId) {
    for (auto& m : media)
        if (m.id == mediaId) return &m;
    return nullptr;
}

const MediaItem* Project::findMedia(const std::string& mediaId) const {
    return const_cast<Project*>(this)->findMedia(mediaId);
}

Timeline* Project::findTimeline(const std::string& timelineId) {
    for (auto& t : timelines)
        if (t.id == timelineId) return &t;
    return nullptr;
}

const Timeline* Project::findTimeline(const std::string& timelineId) const {
    return const_cast<Project*>(this)->findTimeline(timelineId);
}

Timeline* Project::activeTimeline() {
    Timeline* t = findTimeline(activeTimelineId);
    if (!t && !timelines.empty()) t = &timelines.front();
    return t;
}

const Timeline* Project::activeTimeline() const {
    return const_cast<Project*>(this)->activeTimeline();
}

}  // namespace up

namespace up {

ColorSpace mediaColorSpace(const MediaItem& item) {
    if (item.colorSpace) return *item.colorSpace;
    return detectColorSpace(item.info.colorPrimaries, item.info.colorTransfer, item.info.isStill);
}

}  // namespace up
