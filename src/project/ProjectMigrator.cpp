#include "project/ProjectMigrator.h"

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "project/Project.h"

namespace up {

ProjectMigrator::ProjectMigrator(int currentVersion) : currentVersion_(currentVersion) {}

const ProjectMigrator& ProjectMigrator::standard() {
    static const ProjectMigrator migrator = [] {
        ProjectMigrator m(Project::kFormatVersion);
        // v1 -> v2: timelines gain record marks and source-patching targets; media gains
        // source marks. Existing timelines target their first video and audio track,
        // which matches how v1 placed media.
        m.addStep(1, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"]) {
                std::string video, audio;
                for (const auto& tr : tl.value("tracks", nlohmann::json::array())) {
                    const std::string kind = tr.value("kind", "");
                    if (kind == "video" && video.empty()) video = tr.value("id", "");
                    if (kind == "audio" && audio.empty()) audio = tr.value("id", "");
                }
                tl["targets"] = {{"video", video}, {"audio", audio}};
                tl["markIn"] = nullptr;
                tl["markOut"] = nullptr;
            }
            for (auto& media : doc["media"]) {
                media["markIn"] = nullptr;
                media["markOut"] = nullptr;
            }
            return Status::success();
        });
        // v2 -> v3: timelines and clips gain marker lists (empty for existing projects).
        m.addStep(2, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"]) {
                tl["markers"] = nlohmann::json::array();
                for (auto& tr : tl["tracks"])
                    for (auto& clip : tr["clips"]) clip["markers"] = nlohmann::json::array();
            }
            return Status::success();
        });
        // v3 -> v4: clips gain an animatable transform; existing clips keep the identity.
        m.addStep(3, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"])
                for (auto& tr : tl["tracks"])
                    for (auto& clip : tr["clips"]) clip["transform"] = nlohmann::json::object();
            return Status::success();
        });
        return m;
    }();
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
