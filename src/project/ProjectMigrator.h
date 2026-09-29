#pragma once

#include <functional>
#include <map>

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

}  // namespace up
