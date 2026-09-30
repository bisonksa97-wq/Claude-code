#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "codec/MediaWriter.h"
#include "core/Result.h"

namespace up::render {

// A named set of delivery settings. Presets never change the picture (resolution,
// frame rate and colour come from the timeline); they choose the container, codecs,
// bit depth and quality.
struct ExportPreset {
    std::string id;           // "h264-web"; unique; letters, digits and '-'
    std::string name;         // "H.264 (web)"
    std::string description;
    std::string extension;    // ".mp4", ".mov", ".mkv", ".wav" or ".png" (image sequence)

    bool video = true;
    std::string videoCodec;   // FFmpeg encoder name
    std::string pixelFormat = "yuv420p";
    int crf = 18;             // for CRF encoders (libx264, libx265)
    int64_t videoBitrate = 0;
    std::map<std::string, std::string> codecOptions;
    std::string codecTag;

    bool audio = true;
    std::string audioCodec = "aac";
    int64_t audioBitrate = 192000;

    bool builtIn = false;

    bool imageSequence() const { return extension == ".png"; }
    // Bits per component written (8 for yuv420p, 10 for yuv420p10le, 16 for rgb48be ...).
    int bitDepth() const;
};

const std::vector<ExportPreset>& builtInPresets();

// Checks a preset's fields, and that FFmpeg knows its pixel format. Readable errors.
Status validatePreset(const ExportPreset& preset);
// Whether this FFmpeg build has the preset's encoders ("" = available, else why not).
std::string presetUnavailableReason(const ExportPreset& preset);

std::string presetToJson(const ExportPreset& preset);
Result<ExportPreset> presetFromJson(const std::string& text);

// Built-in presets followed by every valid *.json preset in `userDirectory` (sorted by
// file name). Invalid files are reported in `problems` and skipped; a user preset may
// not reuse a built-in id.
std::vector<ExportPreset> loadPresets(const std::filesystem::path& userDirectory, std::vector<Error>* problems = nullptr);
// The per-user preset folder: $XDG_CONFIG_HOME/UltimatePost/presets (or ~/.config/...),
// ~/Library/Application Support/UltimatePost/Presets, or %APPDATA%\UltimatePost\Presets.
std::filesystem::path defaultPresetDirectory();
const ExportPreset* findPreset(const std::vector<ExportPreset>& presets, const std::string& id);

// Encoder settings for a preset (size, rate and colour tags are filled in by the export).
EncodeSettings encodeSettingsFor(const ExportPreset& preset);

}  // namespace up::render
