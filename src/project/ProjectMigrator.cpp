#include "project/ProjectMigrator.h"

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "project/Project.h"

namespace up {

ProjectMigrator::ProjectMigrator(int currentVersion) : currentVersion_(currentVersion) {}

const ProjectMigrator& ProjectMigrator::standard() {
    // Version 1 is the first released format; future steps are registered here, e.g.
    //   m.addStep(1, [](nlohmann::json& doc) { ...; return Status::success(); });
    static const ProjectMigrator migrator(Project::kFormatVersion);
    return migrator;
}

void ProjectMigrator::addStep(int fromVersion, Step step) {
    steps_[fromVersion] = std::move(step);
}

Status ProjectMigrator::migrate(nlohmann::json& document) const {
    if (!document.is_object() || !document.contains("formatVersion") || !document["formatVersion"].is_number_integer()) {
        return makeError(ErrorCode::ParseError, "project", "This file is not an Ultimate Post project.",
                         "Open a .uproj file created by Ultimate Post.", "missing integer 'formatVersion'");
    }
    int version = document["formatVersion"].get<int>();
    if (version > currentVersion_) {
        return makeError(ErrorCode::UnsupportedVersion, "project",
                         "This project was created by a newer version of Ultimate Post (format " +
                             std::to_string(version) + ").",
                         "Update Ultimate Post to open this project.",
                         "supported format version: " + std::to_string(currentVersion_));
    }
    if (version < 1) {
        return makeError(ErrorCode::ParseError, "project", "The project file has an invalid format version.",
                         "The file may be damaged; try the .bak or autosave copy.",
                         "formatVersion=" + std::to_string(version));
    }
    while (version < currentVersion_) {
        const auto it = steps_.find(version);
        if (it == steps_.end()) {
            return makeError(ErrorCode::UnsupportedVersion, "project",
                             "No upgrade path exists for project format " + std::to_string(version) + ".",
                             "Open the project with the version of Ultimate Post that created it.");
        }
        UP_TRY(it->second(document));
        ++version;
        document["formatVersion"] = version;
        UP_LOG_INFO(log::sub::Project, "Migrated project document to format " << version);
    }
    return Status::success();
}

}  // namespace up
