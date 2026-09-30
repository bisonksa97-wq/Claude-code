#include "project/ProjectMigrator.h"

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "project/Project.h"

namespace up {

ProjectMigrator::ProjectMigrator(int currentVersion) : currentVersion_(currentVersion) {}

int bitDepthFromPixelFormatName(const std::string& name) {
    // FFmpeg names carry the depth: yuv420p10le, gbrp12le, p010le, rgb48be, rgba64le, gray16be ...
    static const std::pair<const char*, int> patterns[] = {
        {"p16", 16}, {"p14", 14}, {"p12", 12}, {"p10", 10}, {"p010", 10}, {"p016", 16}, {"rgb48", 16}, {"bgr48", 16},
        {"rgba64", 16}, {"bgra64", 16}, {"gray16", 16}, {"gray12", 12}, {"gray10", 10}, {"rgb10", 10}, {"y210", 10}};
    for (const auto& [pattern, depth] : patterns)
        if (name.find(pattern) != std::string::npos) return depth;
    return 8;
}

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
        // v4 -> v5: clips gain optional head/tail transitions (none for existing clips).
        m.addStep(4, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"])
                for (auto& tr : tl["tracks"])
                    for (auto& clip : tr["clips"]) {
                        clip["transitionIn"] = nullptr;
                        clip["transitionOut"] = nullptr;
                    }
            return Status::success();
        });
        // v5 -> v6: tracks gain pan and an audio effect chain (centre, no effects).
        m.addStep(5, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"])
                for (auto& tr : tl["tracks"]) {
                    tr["pan"] = 0.0;
                    tr["effects"] = nlohmann::json::array();
                }
            return Status::success();
        });
        // v6 -> v7: clips gain a primary colour grade (identity for existing clips).
        m.addStep(6, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"])
                for (auto& tr : tl["tracks"])
                    for (auto& clip : tr["clips"]) clip["grade"] = nlohmann::json::object();
            return Status::success();
        });
        // v7 -> v8: grade curves/LUTs live inside "grade" (absent = none); clips gain
        // bypass and grade versions (one version "A"); timelines gain an output LUT
        // and a global grade bypass.
        m.addStep(7, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"]) {
                tl["outputLut"] = nullptr;
                tl["gradesBypassed"] = false;
                for (auto& tr : tl["tracks"])
                    for (auto& clip : tr["clips"]) {
                        clip["gradeBypass"] = false;
                        clip["gradeVersion"] = "A";
                        clip["gradeVersions"] = nlohmann::json::array();
                    }
            }
            return Status::success();
        });
        // v8 -> v9: colour management. Timelines work in Rec.709 (gamma 2.4) with the
        // output in the same space; media use the space detected from their tags.
        m.addStep(8, [](nlohmann::json& doc) {
            for (auto& tl : doc["timelines"]) {
                tl["colorSpace"] = "rec709/bt1886";
                tl["outputColorSpace"] = nullptr;
            }
            for (auto& media : doc["media"]) media["colorSpace"] = nullptr;
            return Status::success();
        });
        // v9 -> v10: media info records the bit depth, which selects 16-bit decoding.
        // Older files only stored the FFmpeg pixel format name, so derive it from that.
        m.addStep(9, [](nlohmann::json& doc) {
            for (auto& media : doc["media"]) {
                if (!media.contains("info") || !media["info"].is_object()) continue;
                media["info"]["bitDepth"] = bitDepthFromPixelFormatName(media["info"].value("pixelFormat", ""));
            }
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
