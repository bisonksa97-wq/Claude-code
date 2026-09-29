#pragma once

#include <filesystem>
#include <string>

#include "core/Result.h"
#include "project/Project.h"

namespace up {

// Reads and writes the native *.uproj format (versioned JSON).
// See docs/project-format.md for the schema.
class ProjectSerializer {
public:
    static std::string toJson(const Project& project, const std::filesystem::path& projectFile = {});
    static Result<Project> fromJson(const std::string& text, const std::filesystem::path& projectFile = {});

    // Transactional save: the previous file is only replaced once the new one is fully on disk.
    static Status save(Project& project, const std::filesystem::path& path);
    static Result<Project> load(const std::filesystem::path& path);
};

}  // namespace up
