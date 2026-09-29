#pragma once

#include <filesystem>

#include "core/MediaInfo.h"
#include "core/Result.h"

namespace up {

// Reads container/stream information from a media file without decoding it.
Result<MediaInfo> probeMedia(const std::filesystem::path& path);

}  // namespace up
