#pragma once

#include <filesystem>
#include <string_view>

#include "core/Result.h"

namespace up {

// Writes `contents` to `path` without ever leaving a partially written file:
// data goes to a temporary sibling, is flushed to disk, then renamed over the
// target. When `keepBackup` is set, the previous version is kept as "<path>.bak".
Status writeFileAtomically(const std::filesystem::path& path, std::string_view contents, bool keepBackup = true);

Result<std::string> readFile(const std::filesystem::path& path);

}  // namespace up
