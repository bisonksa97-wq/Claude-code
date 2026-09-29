#pragma once

#include <string>

#include "core/Result.h"
#include "project/Project.h"

namespace up {

// Builds a stand-alone project that presents one media item as a timeline, so the
// source monitor can reuse the same compositor and playback engine as the program
// monitor. Source frame N is timeline frame N (at the edited timeline's rate), which
// makes viewer positions directly usable as source marks.
Result<Project> makeSourceProject(const Project& project, const std::string& mediaId);

}  // namespace up
