#pragma once

#include <functional>
#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "core/Result.h"

namespace up {

// Upgrades serialized project documents from older format versions.
// A step registered for version N converts an N document into an N+1 document.
class ProjectMigrator {
public:
    using Step = std::function<Status(nlohmann::json& document)>;

    explicit ProjectMigrator(int currentVersion);

    // The migrator used for real project files, with every shipped migration registered.
    static const ProjectMigrator& standard();

    void addStep(int fromVersion, Step step);
    int currentVersion() const { return currentVersion_; }

    // Migrates `document` in place up to currentVersion(). Sets "formatVersion" accordingly.
    Status migrate(nlohmann::json& document) const;

private:
    int currentVersion_;
    std::map<int, Step> steps_;
};

// Bits per component encoded in an FFmpeg pixel format name ("yuv420p10le" -> 10);
// 8 when the name carries no depth. Used to upgrade projects saved before bitDepth existed.
int bitDepthFromPixelFormatName(const std::string& name);

}  // namespace up
