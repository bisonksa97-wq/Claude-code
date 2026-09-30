#include "project/ProjectSerializer.h"

#include <nlohmann/json.hpp>

#include "core/AtomicFile.h"
#include "core/Log.h"
#include "project/ProjectMigrator.h"

namespace up {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

template <typename T>
json optionalToJson(const std::optional<T>& v) {
    return v ? json(*v) : json(nullptr);
}

template <typename T>
std::optional<T> optionalFromJson(const json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    return j.at(key).get<T>();
}

std::string pathToUtf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

fs::path pathFromUtf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

json toJson(const MediaInfo& i) {
    return json{{"container", i.container},     {"durationSeconds", i.durationSeconds},
                {"fileSize", i.fileSize},       {"hasVideo", i.hasVideo},
                {"videoCodec", i.videoCodec},   {"width", i.width},
                {"height", i.height},           {"frameRate", i.frameRate.toString()},
                {"pixelFormat", i.pixelFormat}, {"isStill", i.isStill}, {"bitDepth", i.bitDepth},
                {"colorPrimaries", i.colorPrimaries}, {"colorTransfer", i.colorTransfer},
                {"colorMatrix", i.colorMatrix}, {"colorRange", i.colorRange},
                {"masteringMaxLuminance", i.masteringMaxLuminance}, {"masteringMinLuminance", i.masteringMinLuminance},
                {"maxCll", i.maxCll}, {"maxFall", i.maxFall},
                {"hasAudio", i.hasAudio},       {"audioCodec", i.audioCodec},
                {"sampleRate", i.sampleRate},   {"channels", i.channels},
                {"timecode", i.timecode}};
}

MediaInfo mediaInfoFromJson(const json& j) {
    MediaInfo i;
    i.container = j.value("container", "");
    i.durationSeconds = j.value("durationSeconds", 0.0);
    i.fileSize = j.value("fileSize", int64_t{0});
    i.hasVideo = j.value("hasVideo", false);
    i.videoCodec = j.value("videoCodec", "");
    i.width = j.value("width", 0);
    i.height = j.value("height", 0);
    i.frameRate = Rational::parse(j.value("frameRate", "0/1"));
    i.pixelFormat = j.value("pixelFormat", "");
    i.isStill = j.value("isStill", false);
    i.bitDepth = j.value("bitDepth", 8);
    i.colorPrimaries = j.value("colorPrimaries", "");
    i.colorTransfer = j.value("colorTransfer", "");
    i.colorMatrix = j.value("colorMatrix", "");
    i.colorRange = j.value("colorRange", "");
    i.masteringMaxLuminance = j.value("masteringMaxLuminance", 0.0);
    i.masteringMinLuminance = j.value("masteringMinLuminance", 0.0);
    i.maxCll = j.value("maxCll", 0);
    i.maxFall = j.value("maxFall", 0);
    i.hasAudio = j.value("hasAudio", false);
    i.audioCodec = j.value("audioCodec", "");
    i.sampleRate = j.value("sampleRate", 0);
    i.channels = j.value("channels", 0);
    i.timecode = j.value("timecode", "");
    return i;
}

json toJson(const std::vector<Marker>& markers) {
    json out = json::array();
    for (const auto& m : markers)
        out.push_back(json{{"id", m.id},     {"frame", m.frame},     {"duration", m.duration},
                           {"name", m.name}, {"comment", m.comment}, {"color", toString(m.color)}});
    return out;
}

std::vector<Marker> markersFromJson(const json& j) {
    std::vector<Marker> out;
    for (const auto& mj : j) {
        Marker m;
        m.id = mj.at("id").get<std::string>();
        m.frame = mj.at("frame").get<FrameIndex>();
        m.duration = mj.value("duration", FrameIndex{0});
        m.name = mj.value("name", "");
        m.comment = mj.value("comment", "");
        m.color = markerColorFromString(mj.value("color", "blue")).value_or(MarkerColor::Blue);
        out.push_back(std::move(m));
    }
    return out;
}

// Only parameters that differ from their defaults are written, keeping files readable.
json toJson(const ClipTransform& t) {
    json out = json::object();
    for (ClipParam p : kAllClipParams) {
        const AnimatedValue& v = t[p];
        if (!v.animated() && v.value == paramInfo(p).defaultValue) continue;
        json keys = json::array();
        for (const auto& k : v.keys)
            keys.push_back(json{{"frame", k.frame}, {"value", k.value}, {"interpolation", toString(k.interpolation)}});
        out[paramInfo(p).id] = json{{"value", v.value}, {"keys", keys}};
    }
    return out;
}

ClipTransform transformFromJson(const json& j) {
    ClipTransform t;
    for (ClipParam p : kAllClipParams) {
        if (!j.contains(paramInfo(p).id)) continue;
        const json& pj = j.at(paramInfo(p).id);
        AnimatedValue& v = t[p];
        v.value = pj.value("value", paramInfo(p).defaultValue);
        for (const auto& kj : pj.value("keys", json::array())) {
            v.setKey(kj.at("frame").get<FrameIndex>(), kj.at("value").get<double>(),
                     interpolationFromString(kj.value("interpolation", "linear")).value_or(Interpolation::Linear));
        }
    }
    return t;
}

json toJson(const std::optional<Transition>& t) {
    if (!t) return nullptr;
    return json{{"kind", toString(t->kind)}, {"duration", t->duration}, {"alignment", toString(t->alignment)}};
}

std::optional<Transition> transitionFromJson(const json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    const json& tj = j.at(key);
    Transition t;
    t.kind = transitionKindFromString(tj.value("kind", "dissolve")).value_or(TransitionKind::Dissolve);
    t.duration = tj.at("duration").get<FrameIndex>();
    t.alignment = transitionAlignmentFromString(tj.value("alignment", "center")).value_or(TransitionAlignment::Center);
    return t;
}

// Grade parameters, written like the transform: only non-default values.
// Colour spaces are written as ids ("rec709/bt1886"). An unknown id (a hand-edited
// file) falls back to the default with a warning rather than failing the load.
std::optional<ColorSpace> colorSpaceFromJson(const json& j, const char* key) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    const std::string id = j.at(key).get<std::string>();
    auto space = ColorSpace::fromId(id);
    if (!space) UP_LOG_WARN(log::sub::Project, "Unknown colour space '" << id << "'; using Rec.709 (gamma 2.4).");
    return space.value_or(ColorSpace{});
}

json lutToJson(const std::optional<LutRef>& lut, const fs::path& baseDir) {
    if (!lut) return nullptr;
    fs::path rel = lut->relativePath;
    if (!baseDir.empty() && !lut->path.empty()) {
        std::error_code ec;
        rel = fs::relative(lut->path, baseDir, ec);
        if (ec) rel.clear();
    }
    return json{{"path", pathToUtf8(lut->path)}, {"relativePath", pathToUtf8(rel)}};
}

// LUTs resolve like media: the absolute path if it exists, else relative to the project.
std::optional<LutRef> lutFromJson(const json& j, const char* key, const fs::path& baseDir) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    LutRef ref;
    ref.path = pathFromUtf8(j.at(key).value("path", ""));
    ref.relativePath = pathFromUtf8(j.at(key).value("relativePath", ""));
    std::error_code ec;
    if (!baseDir.empty() && !ref.relativePath.empty() && !fs::is_regular_file(ref.path, ec)) {
        const fs::path candidate = (baseDir / ref.relativePath).lexically_normal();
        if (fs::is_regular_file(candidate, ec)) ref.path = candidate;
    }
    return ref;
}

json gradeToJson(const ClipGrade& g, const fs::path& baseDir) {
    json out = json::object();
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const auto p = static_cast<GradeParam>(i);
        const AnimatedValue& v = g[p];
        if (!v.animated() && v.value == gradeInfo(p).defaultValue) continue;
        json keys = json::array();
        for (const auto& k : v.keys)
            keys.push_back(json{{"frame", k.frame}, {"value", k.value}, {"interpolation", toString(k.interpolation)}});
        out[gradeInfo(p).id] = json{{"value", v.value}, {"keys", keys}};
    }
    json curves = json::object();
    for (std::size_t i = 0; i < kCurveKindCount; ++i) {
        if (g.curves[i].empty()) continue;
        json points = json::array();
        for (const auto& pt : g.curves[i]) points.push_back(json::array({pt.x, pt.y}));
        curves[curveId(static_cast<CurveKind>(i))] = points;
    }
    if (!curves.empty()) out["curves"] = curves;
    if (g.lut) out["lut"] = lutToJson(g.lut, baseDir);
    return out;
}

ClipGrade gradeFromJson(const json& j, const fs::path& baseDir) {
    ClipGrade g;
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const auto p = static_cast<GradeParam>(i);
        if (!j.contains(gradeInfo(p).id)) continue;
        const json& pj = j.at(gradeInfo(p).id);
        AnimatedValue& v = g[p];
        v.value = pj.value("value", gradeInfo(p).defaultValue);
        for (const auto& kj : pj.value("keys", json::array()))
            v.setKey(kj.at("frame").get<FrameIndex>(), kj.at("value").get<double>(),
                     interpolationFromString(kj.value("interpolation", "linear")).value_or(Interpolation::Linear));
    }
    const json curves = j.value("curves", json::object());
    for (const auto& [id, points] : curves.items()) {
        const auto kind = curveKindFromString(id);
        if (!kind) continue;  // unknown curves from newer builds are ignored
        for (const auto& pt : points) g.curve(*kind).push_back(CurvePoint{pt.at(0).get<double>(), pt.at(1).get<double>()});
    }
    g.lut = lutFromJson(j, "lut", baseDir);
    return g;
}

json toJson(const Clip& c, const fs::path& baseDir) {
    json versions = json::array();
    for (const auto& v : c.gradeVersions) versions.push_back(json{{"name", v.name}, {"grade", gradeToJson(v.grade, baseDir)}});
    return json{{"id", c.id},         {"mediaId", c.mediaId},   {"name", c.name},
                {"start", c.start},   {"duration", c.duration}, {"sourceIn", c.sourceIn},
                {"sourceLength", c.sourceLength}, {"linkId", c.linkId}, {"enabled", c.enabled},
                {"gainDb", c.gainDb}, {"markers", toJson(c.markers)}, {"transform", toJson(c.transform)},
                {"transitionIn", toJson(c.transitionIn)}, {"transitionOut", toJson(c.transitionOut)},
                {"grade", gradeToJson(c.grade, baseDir)}, {"gradeBypass", c.gradeBypass},
                {"gradeVersion", c.gradeVersion}, {"gradeVersions", versions}};
}

Clip clipFromJson(const json& j, const fs::path& baseDir) {
    Clip c;
    c.id = j.at("id").get<std::string>();
    c.mediaId = j.value("mediaId", "");
    c.name = j.value("name", "");
    c.start = j.at("start").get<FrameIndex>();
    c.duration = j.at("duration").get<FrameIndex>();
    c.sourceIn = j.value("sourceIn", FrameIndex{0});
    c.sourceLength = j.value("sourceLength", FrameIndex{0});
    c.linkId = j.value("linkId", "");
    c.enabled = j.value("enabled", true);
    c.gainDb = j.value("gainDb", 0.0);
    c.markers = markersFromJson(j.value("markers", json::array()));
    c.transform = transformFromJson(j.value("transform", json::object()));
    c.transitionIn = transitionFromJson(j, "transitionIn");
    c.transitionOut = transitionFromJson(j, "transitionOut");
    c.grade = gradeFromJson(j.value("grade", json::object()), baseDir);
    c.gradeBypass = j.value("gradeBypass", false);
    c.gradeVersion = j.value("gradeVersion", "A");
    for (const auto& vj : j.value("gradeVersions", json::array()))
        c.gradeVersions.push_back(NamedGrade{vj.value("name", ""), gradeFromJson(vj.value("grade", json::object()), baseDir)});
    return c;
}

json toJson(const Track& t, const fs::path& baseDir) {
    json clips = json::array();
    for (const auto& c : t.clips) clips.push_back(toJson(c, baseDir));
    json effects = json::array();
    for (const auto& fx : t.effects)
        effects.push_back(json{{"id", fx.id}, {"type", fx.type}, {"enabled", fx.enabled}, {"params", fx.params}});
    return json{{"id", t.id},         {"kind", toString(t.kind)}, {"name", t.name},
                {"enabled", t.enabled}, {"locked", t.locked},     {"muted", t.muted},
                {"solo", t.solo},     {"gainDb", t.gainDb},       {"pan", t.pan},
                {"effects", effects}, {"clips", clips}};
}

Track trackFromJson(const json& j, const fs::path& baseDir) {
    Track t;
    t.id = j.at("id").get<std::string>();
    t.kind = j.at("kind").get<std::string>() == "audio" ? TrackKind::Audio : TrackKind::Video;
    t.name = j.value("name", "");
    t.enabled = j.value("enabled", true);
    t.locked = j.value("locked", false);
    t.muted = j.value("muted", false);
    t.solo = j.value("solo", false);
    t.gainDb = j.value("gainDb", 0.0);
    t.pan = j.value("pan", 0.0);
    for (const auto& fj : j.value("effects", json::array())) {
        audio::EffectSpec fx;
        fx.id = fj.at("id").get<std::string>();
        fx.type = fj.at("type").get<std::string>();
        fx.enabled = fj.value("enabled", true);
        fx.params = fj.value("params", std::map<std::string, double>{});
        t.effects.push_back(std::move(fx));
    }
    for (const auto& c : j.value("clips", json::array())) t.clips.push_back(clipFromJson(c, baseDir));
    return t;
}

json toJson(const Timeline& t, const fs::path& baseDir) {
    json tracks = json::array();
    for (const auto& tr : t.tracks) tracks.push_back(toJson(tr, baseDir));
    return json{{"id", t.id},
                {"name", t.name},
                {"frameRate", t.frameRate.toString()},
                {"width", t.width},
                {"height", t.height},
                {"sampleRate", t.sampleRate},
                {"markIn", optionalToJson(t.markIn)},
                {"markOut", optionalToJson(t.markOut)},
                {"targets", {{"video", t.videoTarget}, {"audio", t.audioTarget}}},
                {"markers", toJson(t.markers)},
                {"outputLut", lutToJson(t.outputLut, baseDir)},
                {"gradesBypassed", t.gradesBypassed},
                {"colorSpace", t.colorSpace.id()},
                {"outputColorSpace", t.outputColorSpace ? json(t.outputColorSpace->id()) : json(nullptr)},
                {"tracks", tracks}};
}

Timeline timelineFromJson(const json& j, const fs::path& baseDir) {
    Timeline t;
    t.id = j.at("id").get<std::string>();
    t.name = j.value("name", "");
    t.frameRate = Rational::parse(j.at("frameRate").get<std::string>());
    t.width = j.value("width", 1920);
    t.height = j.value("height", 1080);
    t.sampleRate = j.value("sampleRate", 48000);
    for (const auto& tr : j.value("tracks", json::array())) t.tracks.push_back(trackFromJson(tr, baseDir));
    t.markIn = optionalFromJson<FrameIndex>(j, "markIn");
    t.markOut = optionalFromJson<FrameIndex>(j, "markOut");
    const json targets = j.value("targets", json::object());
    t.videoTarget = targets.value("video", "");
    t.audioTarget = targets.value("audio", "");
    t.markers = markersFromJson(j.value("markers", json::array()));
    t.outputLut = lutFromJson(j, "outputLut", baseDir);
    t.gradesBypassed = j.value("gradesBypassed", false);
    t.colorSpace = colorSpaceFromJson(j, "colorSpace").value_or(ColorSpace{});
    t.outputColorSpace = colorSpaceFromJson(j, "outputColorSpace");
    return t;
}

}  // namespace

std::string ProjectSerializer::toJson(const Project& p, const fs::path& projectFile) {
    const fs::path baseDir = projectFile.empty() ? fs::path() : fs::absolute(projectFile).parent_path();
    json media = json::array();
    for (const auto& m : p.media) {
        fs::path rel = m.relativePath;
        if (!baseDir.empty() && !m.path.empty()) {
            std::error_code ec;
            rel = fs::relative(m.path, baseDir, ec);
            if (ec) rel.clear();
        }
        media.push_back(json{{"id", m.id},
                             {"name", m.name},
                             {"path", pathToUtf8(m.path)},
                             {"relativePath", pathToUtf8(rel)},
                             {"binId", m.binId},
                             {"info", ::up::toJson(m.info)},
                             {"rating", m.rating},
                             {"keywords", m.keywords},
                             {"comment", m.comment},
                             {"importedAt", m.importedAt},
                             {"markIn", optionalToJson(m.markIn)},
                             {"markOut", optionalToJson(m.markOut)},
                             {"colorSpace", m.colorSpace ? json(m.colorSpace->id()) : json(nullptr)}});
    }
    json bins = json::array();
    for (const auto& b : p.bins) bins.push_back(json{{"id", b.id}, {"name", b.name}, {"parentId", b.parentId}});
    json timelines = json::array();
    for (const auto& t : p.timelines) timelines.push_back(::up::toJson(t, baseDir));

    json doc{{"format", "ultimatepost.project"},
             {"formatVersion", Project::kFormatVersion},
             {"project",
              {{"id", p.id},
               {"name", p.name},
               {"createdAt", p.createdAt},
               {"modifiedAt", p.modifiedAt},
               {"settings",
                {{"frameRate", p.settings.frameRate.toString()},
                 {"width", p.settings.width},
                 {"height", p.settings.height},
                 {"sampleRate", p.settings.sampleRate}}},
               {"activeTimelineId", p.activeTimelineId}}},
             {"bins", bins},
             {"media", media},
             {"timelines", timelines}};
    return doc.dump(2);
}

Result<Project> ProjectSerializer::fromJson(const std::string& text, const fs::path& projectFile) {
    json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        return makeError(ErrorCode::ParseError, "project", "The project file is damaged and cannot be read.",
                         "Open the automatic backup (.uproj.bak) or the autosave copy instead.",
                         "invalid JSON");
    }
    UP_TRY(ProjectMigrator::standard().migrate(doc));
    try {
        Project p;
        const json& pj = doc.at("project");
        p.id = pj.at("id").get<std::string>();
        p.name = pj.value("name", "Untitled");
        p.createdAt = pj.value("createdAt", "");
        p.modifiedAt = pj.value("modifiedAt", "");
        const json& sj = pj.at("settings");
        p.settings.frameRate = Rational::parse(sj.at("frameRate").get<std::string>());
        p.settings.width = sj.value("width", 1920);
        p.settings.height = sj.value("height", 1080);
        p.settings.sampleRate = sj.value("sampleRate", 48000);
        p.activeTimelineId = pj.value("activeTimelineId", "");
        for (const auto& b : doc.value("bins", json::array()))
            p.bins.push_back(Bin{b.at("id").get<std::string>(), b.value("name", ""), b.value("parentId", "")});
        for (const auto& mj : doc.value("media", json::array())) {
            MediaItem m;
            m.id = mj.at("id").get<std::string>();
            m.name = mj.value("name", "");
            m.path = pathFromUtf8(mj.value("path", ""));
            m.relativePath = pathFromUtf8(mj.value("relativePath", ""));
            m.binId = mj.value("binId", "");
            m.info = mediaInfoFromJson(mj.value("info", json::object()));
            m.rating = mj.value("rating", 0);
            m.keywords = mj.value("keywords", std::vector<std::string>{});
            m.comment = mj.value("comment", "");
            m.importedAt = mj.value("importedAt", "");
            m.markIn = optionalFromJson<double>(mj, "markIn");
            m.markOut = optionalFromJson<double>(mj, "markOut");
            m.colorSpace = colorSpaceFromJson(mj, "colorSpace");
            p.media.push_back(std::move(m));
        }
        for (const auto& tj : doc.value("timelines", json::array())) {
            Timeline t = timelineFromJson(tj, projectFile.empty() ? fs::path() : fs::absolute(projectFile).parent_path());
            UP_TRY(t.validate());
            p.timelines.push_back(std::move(t));
        }
        p.filePath = projectFile;
        return p;
    } catch (const json::exception& e) {
        return makeError(ErrorCode::ParseError, "project", "The project file is missing required information.",
                         "Open the automatic backup (.uproj.bak) or the autosave copy instead.", e.what());
    }
}

Status ProjectSerializer::save(Project& project, const fs::path& path) {
    project.modifiedAt = currentUtcTimestamp();
    const std::string text = toJson(project, path);
    UP_TRY(writeFileAtomically(path, text));
    project.filePath = path;
    UP_LOG_INFO(log::sub::Project, "Saved project '" << project.name << "' to " << path.string());
    return Status::success();
}

Result<Project> ProjectSerializer::load(const fs::path& path) {
    auto text = readFile(path);
    if (!text.ok()) return text.error();
    auto project = fromJson(text.value(), path);
    if (project.ok()) UP_LOG_INFO(log::sub::Project, "Loaded project from " << path.string());
    return project;
}

}  // namespace up
