#include "render/ExportPresets.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <nlohmann/json.hpp>

extern "C" {
#include <libavutil/pixdesc.h>
}

#include "core/AtomicFile.h"

namespace up::render {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

ExportPreset make(std::string id, std::string name, std::string description, std::string extension) {
    ExportPreset p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.description = std::move(description);
    p.extension = std::move(extension);
    p.builtIn = true;
    return p;
}

Error presetError(const std::string& message, const std::string& suggestion = "Fix the preset file and try again.") {
    return makeError(ErrorCode::InvalidArgument, "export", message, suggestion);
}

}  // namespace

int ExportPreset::bitDepth() const {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(av_get_pix_fmt(pixelFormat.c_str()));
    return d ? d->comp[0].depth : 8;
}

const std::vector<ExportPreset>& builtInPresets() {
    static const std::vector<ExportPreset> presets = [] {
        std::vector<ExportPreset> v;
        {
            ExportPreset p = make("h264-web", "H.264 (web)", "MP4, H.264 8-bit 4:2:0 at CRF 20, AAC 192 kb/s. Plays everywhere.", ".mp4");
            p.videoCodec = "libx264";
            p.crf = 20;
            v.push_back(p);
        }
        {
            ExportPreset p = make("h264-high", "H.264 (high quality)", "MP4, H.264 8-bit 4:2:0 at CRF 14, AAC 320 kb/s.", ".mp4");
            p.videoCodec = "libx264";
            p.crf = 14;
            p.audioBitrate = 320000;
            v.push_back(p);
        }
        {
            ExportPreset p = make("hevc-10bit", "HEVC 10-bit", "MP4, HEVC Main10 4:2:0 at CRF 18 (tagged hvc1), AAC 256 kb/s. Use for HDR (PQ/HLG) delivery.", ".mp4");
            p.videoCodec = "libx265";
            p.pixelFormat = "yuv420p10le";
            p.codecTag = "hvc1";
            p.audioBitrate = 256000;
            v.push_back(p);
        }
        {
            ExportPreset p = make("prores-422hq", "ProRes 422 HQ", "QuickTime, Apple ProRes 422 HQ 10-bit 4:2:2, 24-bit PCM. Mastering and interchange.", ".mov");
            p.videoCodec = "prores_ks";
            p.pixelFormat = "yuv422p10le";
            p.codecOptions = {{"profile", "3"}, {"vendor", "apl0"}};
            p.audioCodec = "pcm_s24le";
            v.push_back(p);
        }
        {
            ExportPreset p = make("prores-4444", "ProRes 4444", "QuickTime, Apple ProRes 4444 10-bit 4:4:4, 24-bit PCM.", ".mov");
            p.videoCodec = "prores_ks";
            p.pixelFormat = "yuv444p10le";
            p.codecOptions = {{"profile", "4"}, {"vendor", "apl0"}};
            p.audioCodec = "pcm_s24le";
            v.push_back(p);
        }
        {
            ExportPreset p = make("ffv1-archive", "FFV1 lossless 10-bit", "Matroska, FFV1 lossless 10-bit RGB, 24-bit PCM. Archiving and exact intermediates.", ".mkv");
            p.videoCodec = "ffv1";
            p.pixelFormat = "gbrp10le";
            p.codecOptions = {{"level", "3"}};
            p.audioCodec = "pcm_s24le";
            v.push_back(p);
        }
        {
            ExportPreset p = make("wav-24", "WAV audio (24-bit)", "The mix only, 24-bit PCM WAV at the timeline sample rate.", ".wav");
            p.video = false;
            p.videoCodec.clear();
            p.audioCodec = "pcm_s24le";
            v.push_back(p);
        }
        {
            ExportPreset p = make("png-16", "PNG sequence (16-bit)", "One 16-bit RGB PNG per frame in a folder; no audio.", ".png");
            p.videoCodec = "png";
            p.pixelFormat = "rgb48be";
            p.audio = false;
            v.push_back(p);
        }
        return v;
    }();
    return presets;
}

Status validatePreset(const ExportPreset& p) {
    if (p.id.empty() || !std::all_of(p.id.begin(), p.id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; }))
        return presetError("A preset id must be letters, digits and '-' (got '" + p.id + "').");
    if (p.name.empty()) return presetError("Preset '" + p.id + "' has no name.");
    static const std::vector<std::string> extensions = {".mp4", ".mov", ".mkv", ".wav", ".png"};
    if (std::find(extensions.begin(), extensions.end(), p.extension) == extensions.end())
        return presetError("Preset '" + p.id + "' has an unsupported extension '" + p.extension + "'.",
                           "Use .mp4, .mov, .mkv, .wav or .png.");
    if (!p.video && !p.audio) return presetError("Preset '" + p.id + "' writes neither video nor audio.");
    if (p.extension == ".wav" && p.video) return presetError("A .wav preset cannot contain video.");
    if (p.imageSequence() && p.audio) return presetError("An image sequence cannot contain audio.");
    if (p.video) {
        if (p.videoCodec.empty()) return presetError("Preset '" + p.id + "' writes video but names no video codec.");
        if (av_get_pix_fmt(p.pixelFormat.c_str()) == AV_PIX_FMT_NONE)
            return presetError("Preset '" + p.id + "' uses an unknown pixel format '" + p.pixelFormat + "'.");
        if (p.crf < 0 || p.crf > 51) return presetError("Preset '" + p.id + "': crf must be between 0 and 51.");
        if (!p.codecTag.empty() && p.codecTag.size() != 4) return presetError("A codec tag has four characters (e.g. hvc1).");
    }
    if (p.audio && p.audioCodec.empty()) return presetError("Preset '" + p.id + "' writes audio but names no audio codec.");
    if (p.videoBitrate < 0 || p.audioBitrate < 0) return presetError("Bitrates cannot be negative.");
    return Status::success();
}

std::string presetUnavailableReason(const ExportPreset& p) {
    if (p.video && !encoderAvailable(p.videoCodec))
        return "this FFmpeg build has no '" + p.videoCodec + "' video encoder";
    if (p.audio && !encoderAvailable(p.audioCodec))
        return "this FFmpeg build has no '" + p.audioCodec + "' audio encoder";
    return {};
}

std::string presetToJson(const ExportPreset& p) {
    json j{{"id", p.id},           {"name", p.name},           {"description", p.description},
           {"extension", p.extension}, {"video", p.video},     {"videoCodec", p.videoCodec},
           {"pixelFormat", p.pixelFormat}, {"crf", p.crf},     {"videoBitrate", p.videoBitrate},
           {"codecOptions", p.codecOptions}, {"codecTag", p.codecTag}, {"audio", p.audio},
           {"audioCodec", p.audioCodec}, {"audioBitrate", p.audioBitrate}};
    return j.dump(2);
}

Result<ExportPreset> presetFromJson(const std::string& text) {
    try {
        const json j = json::parse(text);
        ExportPreset p;
        p.id = j.at("id").get<std::string>();
        p.name = j.value("name", p.id);
        p.description = j.value("description", "");
        p.extension = j.at("extension").get<std::string>();
        p.video = j.value("video", true);
        p.videoCodec = j.value("videoCodec", "");
        p.pixelFormat = j.value("pixelFormat", "yuv420p");
        p.crf = j.value("crf", 18);
        p.videoBitrate = j.value("videoBitrate", int64_t{0});
        p.codecOptions = j.value("codecOptions", std::map<std::string, std::string>{});
        p.codecTag = j.value("codecTag", "");
        p.audio = j.value("audio", true);
        p.audioCodec = j.value("audioCodec", "aac");
        p.audioBitrate = j.value("audioBitrate", int64_t{192000});
        UP_TRY(validatePreset(p));
        return p;
    } catch (const json::exception& e) {
        return makeError(ErrorCode::ParseError, "export", "The preset is not valid JSON or misses a required field.",
                         "Presets need at least \"id\" and \"extension\".", e.what());
    }
}

std::vector<ExportPreset> loadPresets(const fs::path& userDirectory, std::vector<Error>* problems) {
    std::vector<ExportPreset> presets = builtInPresets();
    std::error_code ec;
    if (userDirectory.empty() || !fs::is_directory(userDirectory, ec)) return presets;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(userDirectory, ec))
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        auto report = [&](Error e) {
            e.message = file.filename().string() + ": " + e.message;
            if (problems) problems->push_back(std::move(e));
        };
        auto text = readFile(file);
        if (!text.ok()) {
            report(text.error());
            continue;
        }
        auto preset = presetFromJson(text.value());
        if (!preset.ok()) {
            report(preset.error());
            continue;
        }
        if (findPreset(presets, preset.value().id)) {
            report(presetError("The preset id '" + preset.value().id + "' is already used.", "Give the preset a different id."));
            continue;
        }
        presets.push_back(std::move(preset.value()));
    }
    return presets;
}

fs::path defaultPresetDirectory() {
    auto env = [](const char* name) -> fs::path {
        const char* v = std::getenv(name);
        return v && *v ? fs::path(v) : fs::path();
    };
#if defined(_WIN32)
    if (auto appData = env("APPDATA"); !appData.empty()) return appData / "UltimatePost" / "Presets";
#elif defined(__APPLE__)
    if (auto home = env("HOME"); !home.empty()) return home / "Library" / "Application Support" / "UltimatePost" / "Presets";
#else
    if (auto config = env("XDG_CONFIG_HOME"); !config.empty()) return config / "UltimatePost" / "presets";
    if (auto home = env("HOME"); !home.empty()) return home / ".config" / "UltimatePost" / "presets";
#endif
    return {};
}

const ExportPreset* findPreset(const std::vector<ExportPreset>& presets, const std::string& id) {
    for (const auto& p : presets)
        if (p.id == id) return &p;
    return nullptr;
}

EncodeSettings encodeSettingsFor(const ExportPreset& p) {
    EncodeSettings s;
    s.video = p.video;
    s.videoCodec = p.videoCodec;
    s.pixelFormat = p.pixelFormat;
    s.crf = p.crf;
    s.videoBitrate = p.videoBitrate;
    s.codecOptions = p.codecOptions;
    s.codecTag = p.codecTag;
    s.audio = p.audio;
    s.audioCodec = p.audioCodec;
    s.audioBitrate = p.audioBitrate;
    return s;
}

}  // namespace up::render
