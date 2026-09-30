// ultimatepost: command-line front end to the Ultimate Post engine.
// Every command goes through the same application services (EditorSession) as the UI.

#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>

#include <thread>

#include "app/EditorSession.h"
#include "app/MediaAssets.h"
#include "cli/Args.h"
#include "codec/MediaProbe.h"
#include "codec/MediaWriter.h"
#include "core/Log.h"
#include "core/Timecode.h"
#include "media/MediaLibrary.h"
#include "media/SyntheticMedia.h"
#include "render/ExportJob.h"
#include "render/FrameCompositor.h"

namespace fs = std::filesystem;
using namespace up;

namespace {

constexpr const char* kVersion = "0.1.0";

int fail(const Error& e) {
    std::cerr << "error: " << e.toString() << "\n";
    return 1;
}

int usageError(const std::string& message) {
    std::cerr << "usage error: " << message << "\nRun 'ultimatepost help' for usage.\n";
    return 2;
}

Result<std::unique_ptr<EditorSession>> openProject(const std::string& path) {
    return EditorSession::open(path);
}

std::optional<FrameIndex> parseFrame(const std::string& text, FrameRate rate) {
    if (!text.empty() && (text[0] == '-' || text[0] == '+')) {
        auto v = parseTimecode(text.substr(1), rate);
        if (!v) return std::nullopt;
        return text[0] == '-' ? -*v : *v;
    }
    return parseTimecode(text, rate);
}

// Media reference: full id, unique id prefix, or exact file name.
Result<std::string> resolveMedia(const Project& p, const std::string& ref) {
    std::vector<const MediaItem*> matches;
    for (const auto& m : p.media)
        if (m.id == ref || m.name == ref || (ref.size() >= 4 && m.id.rfind(ref, 0) == 0)) matches.push_back(&m);
    if (matches.size() == 1) return matches.front()->id;
    return makeError(matches.empty() ? ErrorCode::NotFound : ErrorCode::InvalidArgument, "cli",
                     matches.empty() ? "No media matches '" + ref + "'." : "'" + ref + "' matches several media items.",
                     "Use 'ultimatepost info' to list media ids.");
}

// Clip reference: full id, unique id prefix (>= 4 chars), or TRACK:N (1-based, e.g. V1:2).
Result<std::string> resolveClip(const Timeline& tl, const std::string& ref) {
    if (const auto colon = ref.find(':'); colon != std::string::npos) {
        const std::string trackName = ref.substr(0, colon);
        const int index = std::atoi(ref.c_str() + colon + 1);
        for (const auto& t : tl.tracks) {
            if (t.name == trackName && index >= 1 && static_cast<std::size_t>(index) <= t.clips.size())
                return t.clips[static_cast<std::size_t>(index - 1)].id;
        }
    } else {
        std::vector<std::string> matches;
        for (const auto& t : tl.tracks)
            for (const auto& c : t.clips)
                if (c.id == ref || (ref.size() >= 4 && c.id.rfind(ref, 0) == 0)) matches.push_back(c.id);
        if (matches.size() == 1) return matches.front();
    }
    return makeError(ErrorCode::NotFound, "cli", "No single clip matches '" + ref + "'.",
                     "Use a clip id from 'ultimatepost info' or TRACK:N such as V1:2.");
}

bool parseSize(const std::string& s, int& w, int& h) {
    return std::sscanf(s.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0;
}

void printInfo(const EditorSession& session) {
    const Project& p = session.project();
    const Timeline& tl = session.timeline();
    std::cout << "Project: " << p.name << "  (format v" << Project::kFormatVersion << ", id " << p.id << ")\n";
    std::cout << "File:    " << (p.filePath.empty() ? "<unsaved>" : p.filePath.string()) << "\n\n";
    std::cout << "Media (" << p.media.size() << "):\n";
    for (const auto& m : p.media) {
        std::cout << "  " << m.id.substr(0, 8) << "  " << (m.online ? "online " : "OFFLINE") << "  " << m.name << "  ";
        if (m.info.hasVideo)
            std::cout << m.info.width << "x" << m.info.height << " " << m.info.videoCodec << " "
                      << m.info.frameRate.toString() << "fps ";
        if (m.info.hasAudio) std::cout << m.info.audioCodec << " " << m.info.sampleRate << "Hz/" << m.info.channels << "ch ";
        std::cout << m.info.durationSeconds << "s\n";
    }
    std::cout << "\nTimeline: " << tl.name << "  " << tl.width << "x" << tl.height << " @ " << tl.frameRate.toString()
              << "fps, " << tl.sampleRate << "Hz, duration " << formatTimecode(tl.duration(), tl.frameRate) << " ("
              << tl.duration() << " frames)\n";
    auto trackName = [&](const std::string& id) {
        const Track* t = tl.track(id);
        return t ? t->name : std::string("none");
    };
    std::cout << "Targets: video " << trackName(tl.videoTarget) << ", audio " << trackName(tl.audioTarget);
    if (tl.markIn || tl.markOut)
        std::cout << "   Marks: " << (tl.markIn ? std::to_string(*tl.markIn) : "-") << " .. "
                  << (tl.markOut ? std::to_string(*tl.markOut) : "-");
    std::cout << "\n";
    for (const auto& t : tl.tracks) {
        std::cout << "  " << t.name << (t.locked ? " [locked]" : "") << (t.enabled ? "" : " [disabled]")
                  << (t.muted ? " [muted]" : "") << (t.solo ? " [solo]" : "") << "\n";
        int n = 1;
        for (const auto& c : t.clips) {
            std::cout << "    " << t.name << ":" << n++ << "  " << c.id.substr(0, 8) << "  "
                      << formatTimecode(c.start, tl.frameRate) << " - " << formatTimecode(c.end(), tl.frameRate)
                      << "  [" << c.start << "," << c.end() << ")  src " << c.sourceIn << "-" << c.sourceOut() << "  "
                      << c.name << (c.linkId.empty() ? "" : "  link:" + c.linkId.substr(0, 6)) << "\n";
        }
    }
}

int saveAndReport(EditorSession& s, const std::string& what) {
    Status st = s.save();
    if (!st.ok()) return fail(st.error());
    std::cout << what << "\n";
    return 0;
}

void printHelp() {
    std::cout << R"(Ultimate Post command-line interface

Usage: ultimatepost <command> [arguments] [--verbose]

Projects
  new <project.uproj> [--name N] [--size 1920x1080] [--fps 25] [--sample-rate 48000]
  info <project.uproj>
  import <project.uproj> <media files...>
  relink <project.uproj> <media> <new file>

Editing (positions/deltas accept frames or HH:MM:SS:FF; clips accept ids or TRACK:N e.g. V1:2)
  place <project> <media> --at <pos> [--insert] [--in <frames>] [--duration <frames>]
  append <project> <media>
  razor <project> <pos>
  trim <project> <clip> <in|out> <delta> [--ripple]
  roll <project> <left clip> <delta>
  slip <project> <clip> <delta>
  slide <project> <clip> <delta>
  move <project> <clip> <track name> <pos>

Markers (colours: red orange yellow green blue purple)
  marker <project> add <pos> [--name N] [--color C] [--comment T] [--length <frames>] [--clip <clip>]
  marker <project> list
  marker <project> remove <marker id>

Three-point editing (out marks are exclusive; omit an option to clear that mark)
  mark <project> <media> [--in <pos>] [--out <pos>]      source marks
  mark-timeline <project> [--in <pos>] [--out <pos>]     record marks
  target <project> [--video <track>|none] [--audio <track>|none]
  edit <project> <media> [--insert] [--at <pos>]         insert/overwrite at the record marks or --at
  duplicate <project> <clip>              copy a clip (with linked partners) right after itself
  delete <project> <clip...> [--ripple]   remove several clips (and partners) in one step

Transforms (params: positionX positionY scale rotation opacity cropLeft cropRight cropTop cropBottom)
  param <project> <clip> list
  param <project> <clip> set <param> <value> [--at <pos>] [--key]   --key adds a keyframe at --at
  param <project> <clip> unkey <param> --at <pos>
  param <project> <clip> interp <param> linear|hold|ease --at <pos>
  param <project> <clip> reset <param>

Transitions (at a clip's head: with an adjacent clip before it, an edit-point transition; else a fade)
  transition <project> <clip> in|out dissolve|dip <frames> [--align center|start|end] [--solo]
  transition <project> <clip> in|out none                 --solo leaves linked partners alone

Tracks
  track <project> add video|audio [--name N]
  track <project> remove <track> [--force]      --force also deletes the track's clips
  track <project> rename <track> <new name>
  track <project> move <track> <position>       1 = bottom of its kind (V1/A1)
  track <project> gain <track> <dB>
  track <project> pan <track> <-100..100>

Audio effects (types: gain eq3 compressor; effects are numbered from 1 in processing order)
  fx <project> <track> list
  fx <project> <track> add <type>
  fx <project> <track> set <n> <param> <value>
  fx <project> <track> enable|disable|remove <n>
  lift <project> <clip>
  ripple-delete <project> <clip>

Rendering
  export <project> <output.mp4> [--codec libx264] [--crf 18] [--no-audio] [--from <pos>] [--to <pos>]
  render-frame <project> <pos> <output.ppm>

Media analysis and cache (default cache: per-user cache folder; override with --cache-dir)
  analyze <project> [--cache-dir DIR]     generate thumbnails and waveforms for all online media
  cache-info [--cache-dir DIR]
  cache-clear [--cache-dir DIR]           delete derived data (it is regenerated on demand)

Media utilities
  probe <file>
  gen-test-media <output.mp4> [--pattern bars|solid|ramp] [--color R,G,B] [--frames N] [--size WxH]
                 [--fps 25] [--tone 440] [--no-audio]

Other
  help, version
)";
}

using Handler = std::function<int(const cli::Args&)>;

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printHelp();
        return 2;
    }
    const std::string command = argv[1];
    const cli::Args args(argc, argv, 2, {"insert", "ripple", "no-audio", "verbose", "force", "key", "solo"});
    log::setDefaultLevel(args.flag("verbose") ? log::Level::Debug : log::Level::Warning);
    if (!args.unknown().empty()) return usageError("option " + args.unknown().front() + " needs a value");
    const auto& pos = args.positional();

    std::map<std::string, std::pair<std::size_t, Handler>> commands;

    commands["help"] = {0, [](const cli::Args&) { printHelp(); return 0; }};
    commands["version"] = {0, [](const cli::Args&) {
        std::cout << "ultimatepost " << kVersion << " (default video encoder: " << defaultVideoEncoder() << ")\n";
        return 0;
    }};

    commands["new"] = {1, [&](const cli::Args& a) {
        SequenceSettings settings;
        if (auto s = a.option("size"); s && !parseSize(*s, settings.width, settings.height))
            return usageError("--size must look like 1920x1080");
        if (auto f = a.option("fps")) {
            settings.frameRate = Rational::parse(*f);
            if (!settings.frameRate.valid()) return usageError("--fps must look like 25 or 30000/1001");
        }
        if (auto r = a.option("sample-rate")) settings.sampleRate = std::atoi(r->c_str());
        const fs::path path = pos[0];
        auto session = EditorSession::createNew(a.option("name").value_or(path.stem().string()), settings);
        Status st = session->saveAs(path);
        if (!st.ok()) return fail(st.error());
        std::cout << "Created " << path.string() << "\n";
        return 0;
    }};

    commands["info"] = {1, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        printInfo(*s.value());
        return 0;
    }};

    commands["import"] = {2, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        std::vector<fs::path> files(pos.begin() + 1, pos.end());
        const ImportReport report = s.value()->importMedia(files);
        for (const auto& e : report.failures) std::cerr << "warning: " << e.toString() << "\n";
        if (report.importedIds.empty()) return 1;
        for (const auto& id : report.importedIds)
            std::cout << "Imported " << s.value()->project().findMedia(id)->name << " (" << id.substr(0, 8) << ")\n";
        return saveAndReport(*s.value(), "Saved " + pos[0]);
    }};

    commands["relink"] = {3, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto media = resolveMedia(s.value()->project(), pos[1]);
        if (!media.ok()) return fail(media.error());
        Status st = s.value()->relinkMedia(media.value(), pos[2]);
        if (!st.ok()) return fail(st.error());
        return saveAndReport(*s.value(), "Relinked to " + pos[2]);
    }};

    commands["place"] = {2, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto media = resolveMedia(s.value()->project(), pos[1]);
        if (!media.ok()) return fail(media.error());
        const FrameRate rate = s.value()->timeline().frameRate;
        const auto at = parseFrame(a.option("at").value_or("0"), rate);
        const auto in = parseFrame(a.option("in").value_or("0"), rate);
        const auto dur = parseFrame(a.option("duration").value_or("0"), rate);
        if (!at || !in || !dur) return usageError("invalid --at/--in/--duration");
        auto r = s.value()->placeMedia(media.value(), *at, a.flag("insert") ? ops::EditMode::Insert : ops::EditMode::Overwrite,
                                       {}, {}, *in, *dur);
        if (!r.ok()) return fail(r.error());
        return saveAndReport(*s.value(), "Placed " + std::to_string(r.value().size()) + " clip(s)");
    }};

    commands["append"] = {2, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto media = resolveMedia(s.value()->project(), pos[1]);
        if (!media.ok()) return fail(media.error());
        auto r = s.value()->appendMedia(media.value());
        if (!r.ok()) return fail(r.error());
        return saveAndReport(*s.value(), "Appended " + std::to_string(r.value().size()) + " clip(s)");
    }};

    commands["razor"] = {2, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        const auto at = parseFrame(pos[1], s.value()->timeline().frameRate);
        if (!at) return usageError("invalid position " + pos[1]);
        auto r = s.value()->razorAt(*at);
        if (!r.ok()) return fail(r.error());
        return saveAndReport(*s.value(), "Cut " + std::to_string(r.value()) + " clip(s)");
    }};

    auto clipEdit = [&](std::size_t argsNeeded, std::function<Status(EditorSession&, const std::string&, const cli::Args&)> fn,
                        std::string done) -> std::pair<std::size_t, Handler> {
        return {argsNeeded, [&, fn, done](const cli::Args& a) {
                    auto s = openProject(pos[0]);
                    if (!s.ok()) return fail(s.error());
                    auto clip = resolveClip(s.value()->timeline(), pos[1]);
                    if (!clip.ok()) return fail(clip.error());
                    Status st = fn(*s.value(), clip.value(), a);
                    if (!st.ok()) return fail(st.error());
                    return saveAndReport(*s.value(), done);
                }};
    };
    auto deltaArg = [&](EditorSession& s, std::size_t index) -> std::optional<FrameIndex> {
        return parseFrame(pos[index], s.timeline().frameRate);
    };
    auto badDelta = [] { return makeError(ErrorCode::InvalidArgument, "cli", "Invalid frame delta."); };

    commands["trim"] = clipEdit(4, [&](EditorSession& s, const std::string& clip, const cli::Args& a) -> Status {
        const auto d = deltaArg(s, 3);
        if (!d || (pos[2] != "in" && pos[2] != "out")) return badDelta();
        return s.trimClip(clip, pos[2] == "in" ? ops::Edge::In : ops::Edge::Out, *d,
                          a.flag("ripple") ? ops::TrimMode::Ripple : ops::TrimMode::Normal);
    }, "Trimmed");
    commands["roll"] = clipEdit(3, [&](EditorSession& s, const std::string& clip, const cli::Args&) -> Status {
        const auto d = deltaArg(s, 2);
        return d ? s.rollEdit(clip, *d) : Status(badDelta());
    }, "Rolled");
    commands["slip"] = clipEdit(3, [&](EditorSession& s, const std::string& clip, const cli::Args&) -> Status {
        const auto d = deltaArg(s, 2);
        return d ? s.slipClip(clip, *d) : Status(badDelta());
    }, "Slipped");
    commands["slide"] = clipEdit(3, [&](EditorSession& s, const std::string& clip, const cli::Args&) -> Status {
        const auto d = deltaArg(s, 2);
        return d ? s.slideClip(clip, *d) : Status(badDelta());
    }, "Slid");
    commands["move"] = clipEdit(4, [&](EditorSession& s, const std::string& clip, const cli::Args&) -> Status {
        const auto at = deltaArg(s, 3);
        if (!at) return badDelta();
        for (const auto& t : s.timeline().tracks)
            if (t.name == pos[2]) return s.moveClip(clip, t.id, *at);
        return makeError(ErrorCode::NotFound, "cli", "No track named '" + pos[2] + "'.");
    }, "Moved");
    commands["lift"] = clipEdit(2, [](EditorSession& s, const std::string& clip, const cli::Args&) {
        return s.liftClip(clip);
    }, "Lifted");
    commands["ripple-delete"] = clipEdit(2, [](EditorSession& s, const std::string& clip, const cli::Args&) {
        return s.rippleDeleteClip(clip);
    }, "Ripple deleted");

    auto optionalFrame = [&](const cli::Args& a, const char* name, FrameRate rate,
                             bool& bad) -> std::optional<FrameIndex> {
        const auto text = a.option(name);
        if (!text) return std::nullopt;
        const auto f = parseFrame(*text, rate);
        if (!f) bad = true;
        return f;
    };

    commands["mark"] = {2, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto media = resolveMedia(s.value()->project(), pos[1]);
        if (!media.ok()) return fail(media.error());
        bool bad = false;
        const FrameRate rate = s.value()->timeline().frameRate;
        const auto in = optionalFrame(a, "in", rate, bad);
        const auto out = optionalFrame(a, "out", rate, bad);
        if (bad) return usageError("invalid --in/--out");
        Status st = s.value()->setMediaMarks(media.value(), in, out);
        if (!st.ok()) return fail(st.error());
        return saveAndReport(*s.value(), "Marked source");
    }};

    commands["mark-timeline"] = {1, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        bool bad = false;
        const FrameRate rate = s.value()->timeline().frameRate;
        const auto in = optionalFrame(a, "in", rate, bad);
        const auto out = optionalFrame(a, "out", rate, bad);
        if (bad) return usageError("invalid --in/--out");
        Status st = s.value()->setTimelineMarks(in, out);
        if (!st.ok()) return fail(st.error());
        return saveAndReport(*s.value(), "Marked timeline");
    }};

    commands["target"] = {1, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        const Timeline& tl = s.value()->timeline();
        auto resolve = [&](const char* opt, const std::string& current, std::string& out) -> bool {
            const auto v = a.option(opt);
            if (!v) {
                out = current;
                return true;
            }
            if (*v == "none") {
                out.clear();
                return true;
            }
            for (const auto& t : tl.tracks)
                if (t.name == *v) {
                    out = t.id;
                    return true;
                }
            return false;
        };
        std::string video, audio;
        if (!resolve("video", tl.videoTarget, video) || !resolve("audio", tl.audioTarget, audio))
            return usageError("unknown track name (use e.g. V1, A2 or none)");
        Status st = s.value()->setTrackTargets(video, audio);
        if (!st.ok()) return fail(st.error());
        return saveAndReport(*s.value(), "Targets updated");
    }};

    commands["edit"] = {2, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto media = resolveMedia(s.value()->project(), pos[1]);
        if (!media.ok()) return fail(media.error());
        const auto at = parseFrame(a.option("at").value_or("0"), s.value()->timeline().frameRate);
        if (!at) return usageError("invalid --at");
        auto r = s.value()->threePointEdit(media.value(), a.flag("insert") ? ops::EditMode::Insert : ops::EditMode::Overwrite, *at);
        if (!r.ok()) return fail(r.error());
        return saveAndReport(*s.value(), std::string(a.flag("insert") ? "Inserted" : "Overwrote") + " frames [" +
                                             std::to_string(r.value().recordIn) + ", " + std::to_string(r.value().recordOut) + ")");
    }};

    commands["delete"] = {2, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        std::vector<std::string> clips;
        for (std::size_t i = 1; i < pos.size(); ++i) {
            auto clip = resolveClip(s.value()->timeline(), pos[i]);
            if (!clip.ok()) return fail(clip.error());
            clips.push_back(clip.value());
        }
        Status st = a.flag("ripple") ? s.value()->rippleDeleteClips(clips) : s.value()->liftClips(clips);
        if (!st.ok()) return fail(st.error());
        return saveAndReport(*s.value(), "Deleted " + std::to_string(clips.size()) + " clip(s) and their partners");
    }};

    commands["param"] = {3, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        EditorSession& session = *s.value();
        auto clip = resolveClip(session.timeline(), pos[1]);
        if (!clip.ok()) return fail(clip.error());
        const Clip& c = *session.timeline().clip(clip.value());
        const std::string& action = pos[2];
        const FrameRate rate = session.timeline().frameRate;
        if (action == "list") {
            for (ClipParam p : kAllClipParams) {
                const AnimatedValue& v = c.transform[p];
                std::cout << paramInfo(p).id << " = " << v.value << paramInfo(p).unit;
                for (const auto& k : v.keys)
                    std::cout << "  [" << formatTimecode(c.toTimeline(k.frame), rate) << ": " << k.value << " " << toString(k.interpolation) << "]";
                std::cout << "\n";
            }
            return 0;
        }
        if (pos.size() < 4) return usageError("param " + action + " needs a parameter name");
        const auto param = clipParamFromString(pos[3]);
        if (!param) return usageError("unknown parameter '" + pos[3] + "'");
        const auto at = parseFrame(a.option("at").value_or(std::to_string(c.start)), rate);
        if (!at) return usageError("invalid --at");
        Status st = Status::success();
        if (action == "set") {
            if (pos.size() < 5) return usageError("param set needs a value");
            char* end = nullptr;
            const double value = std::strtod(pos[4].c_str(), &end);
            if (end == pos[4].c_str() || *end != '\0') return usageError("invalid value " + pos[4]);
            if (a.flag("key")) st = session.setKeyframe(clip.value(), *param, *at, true);
            if (st.ok()) st = session.setClipParameter(clip.value(), *param, value, *at);
        } else if (action == "unkey") {
            st = session.setKeyframe(clip.value(), *param, *at, false);
        } else if (action == "interp") {
            if (pos.size() < 5) return usageError("param interp needs linear, hold or ease");
            const auto interpolation = interpolationFromString(pos[4]);
            if (!interpolation) return usageError("interpolation must be linear, hold or ease");
            st = session.setKeyframeInterpolation(clip.value(), *param, *at, *interpolation);
        } else if (action == "reset") {
            st = session.resetClipParameter(clip.value(), *param);
        } else {
            return usageError("param actions are list, set, unkey, interp and reset");
        }
        if (!st.ok()) return fail(st.error());
        return saveAndReport(session, "Updated " + std::string(paramInfo(*param).id));
    }};

    commands["transition"] = {4, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto clip = resolveClip(s.value()->timeline(), pos[1]);
        if (!clip.ok()) return fail(clip.error());
        if (pos[2] != "in" && pos[2] != "out") return usageError("the edge must be in or out");
        const ops::Edge edge = pos[2] == "in" ? ops::Edge::In : ops::Edge::Out;
        std::optional<Transition> transition;
        if (pos[3] != "none") {
            const auto kind = transitionKindFromString(pos[3]);
            if (!kind) return usageError("the kind must be dissolve, dip or none");
            if (pos.size() < 5) return usageError("give the length in frames");
            const auto length = parseFrame(pos[4], s.value()->timeline().frameRate);
            const auto align = transitionAlignmentFromString(a.option("align").value_or("center"));
            if (!length || !align) return usageError("invalid length or --align");
            transition = Transition{*kind, *length, *align};
        }
        Status st = s.value()->setTransition(clip.value(), edge, transition, !a.flag("solo"));
        if (!st.ok()) return fail(st.error());
        return saveAndReport(*s.value(), transition ? "Transition set" : "Transition removed");
    }};

    commands["fx"] = {3, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        EditorSession& session = *s.value();
        std::string trackId;
        for (const auto& t : session.timeline().tracks)
            if (t.name == pos[1]) trackId = t.id;
        if (trackId.empty()) return fail(makeError(ErrorCode::NotFound, "cli", "No track named '" + pos[1] + "'."));
        const Track& track = *session.timeline().track(trackId);
        const std::string& action = pos[2];
        if (action == "list") {
            int n = 1;
            for (const auto& fx : track.effects) {
                std::cout << n++ << ". " << fx.type << (fx.enabled ? "" : " (disabled)");
                for (const auto& [k, v] : fx.params) std::cout << "  " << k << "=" << v;
                std::cout << "\n";
            }
            return 0;
        }
        if (pos.size() < 4) return usageError("fx " + action + " needs another argument");
        if (action == "add") {
            auto r = session.addTrackEffect(trackId, pos[3]);
            if (!r.ok()) return fail(r.error());
            return saveAndReport(session, "Added " + pos[3]);
        }
        const int index = std::atoi(pos[3].c_str()) - 1;
        if (index < 0 || index >= static_cast<int>(track.effects.size())) return usageError("no effect number " + pos[3]);
        audio::EffectSpec fx = track.effects[static_cast<std::size_t>(index)];
        Status st = Status::success();
        if (action == "remove") {
            st = session.removeTrackEffect(trackId, fx.id);
        } else if (action == "enable" || action == "disable") {
            fx.enabled = action == "enable";
            st = session.updateTrackEffect(trackId, fx);
        } else if (action == "set") {
            if (pos.size() < 6) return usageError("fx set needs a parameter and a value");
            fx.params[pos[4]] = std::atof(pos[5].c_str());
            st = session.updateTrackEffect(trackId, fx);
        } else {
            return usageError("fx actions are list, add, set, enable, disable and remove");
        }
        if (!st.ok()) return fail(st.error());
        return saveAndReport(session, "Effects updated");
    }};

    commands["track"] = {3, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        EditorSession& session = *s.value();
        const std::string& action = pos[1];
        if (action == "add") {
            if (pos[2] != "video" && pos[2] != "audio") return usageError("track add needs video or audio");
            auto r = session.addTrack(pos[2] == "video" ? TrackKind::Video : TrackKind::Audio, a.option("name").value_or(""));
            if (!r.ok()) return fail(r.error());
            return saveAndReport(session, "Added track " + session.timeline().track(r.value())->name);
        }
        std::string trackId;
        for (const auto& t : session.timeline().tracks)
            if (t.name == pos[2]) trackId = t.id;
        if (trackId.empty()) return fail(makeError(ErrorCode::NotFound, "cli", "No track named '" + pos[2] + "'."));
        Status st = Status::success();
        if (action == "remove") {
            st = session.removeTrack(trackId, a.flag("force"));
        } else if (action == "rename") {
            if (pos.size() < 4) return usageError("track rename needs a new name");
            st = session.renameTrack(trackId, pos[3]);
        } else if (action == "move") {
            if (pos.size() < 4) return usageError("track move needs a position");
            st = session.moveTrack(trackId, std::atoi(pos[3].c_str()) - 1);
        } else if (action == "gain" || action == "pan") {
            if (pos.size() < 4) return usageError("track " + action + " needs a value");
            char* end = nullptr;
            const double v = std::strtod(pos[3].c_str(), &end);
            if (end == pos[3].c_str() || *end != '\0') return usageError("invalid value " + pos[3]);
            TrackState state = TrackState::of(*session.timeline().track(trackId));
            (action == "gain" ? state.gainDb : state.pan) = action == "gain" ? v : v / 100.0;
            st = session.setTrackState(trackId, state);
        } else {
            return usageError("track actions are add, remove, rename and move");
        }
        if (!st.ok()) return fail(st.error());
        return saveAndReport(session, "Track updated");
    }};

    commands["duplicate"] = {2, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        auto clip = resolveClip(s.value()->timeline(), pos[1]);
        if (!clip.ok()) return fail(clip.error());
        auto r = s.value()->duplicateClips({clip.value()});
        if (!r.ok()) return fail(r.error());
        return saveAndReport(*s.value(), "Duplicated " + std::to_string(r.value().size()) + " clip(s)");
    }};

    commands["marker"] = {2, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        EditorSession& session = *s.value();
        const FrameRate rate = session.timeline().frameRate;
        const std::string& action = pos[1];
        if (action == "list") {
            for (const auto& ref : session.markers()) {
                std::cout << ref.marker.id.substr(0, 8) << "  " << formatTimecode(ref.timelineFrame, rate) << "  "
                          << toString(ref.marker.color) << "  " << (ref.clipId.empty() ? "timeline" : "clip " + ref.clipId.substr(0, 8))
                          << "  " << ref.marker.name;
                if (ref.marker.duration > 0) std::cout << "  (" << ref.marker.duration << " frames)";
                if (!ref.marker.comment.empty()) std::cout << "  - " << ref.marker.comment;
                std::cout << "\n";
            }
            return 0;
        }
        if (pos.size() < 3) return usageError("marker " + action + " needs another argument");
        if (action == "remove") {
            std::string id = pos[2];
            for (const auto& ref : session.markers())
                if (ref.marker.id.rfind(pos[2], 0) == 0) id = ref.marker.id;
            Status st = session.removeMarker(id);
            if (!st.ok()) return fail(st.error());
            return saveAndReport(session, "Removed marker");
        }
        if (action != "add") return usageError("marker actions are add, list and remove");
        const auto at = parseFrame(pos[2], rate);
        if (!at) return usageError("invalid position " + pos[2]);
        const auto color = markerColorFromString(a.option("color").value_or("blue"));
        if (!color) return usageError("unknown colour");
        const std::string name = a.option("name").value_or("");
        const std::string comment = a.option("comment").value_or("");
        Result<std::string> id = std::string();
        if (auto clipRef = a.option("clip")) {
            auto clip = resolveClip(session.timeline(), *clipRef);
            if (!clip.ok()) return fail(clip.error());
            id = session.addClipMarker(clip.value(), *at, name, *color, comment);
        } else {
            const auto length = parseFrame(a.option("length").value_or("0"), rate);
            if (!length) return usageError("invalid --length");
            id = session.addMarker(*at, name, *color, comment, *length);
        }
        if (!id.ok()) return fail(id.error());
        return saveAndReport(session, "Added marker " + id.value().substr(0, 8));
    }};

    commands["export"] = {2, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        const Timeline& tl = s.value()->timeline();
        render::ExportOptions options;
        options.output = pos[1];
        options.videoCodec = a.option("codec").value_or("");
        options.crf = std::atoi(a.option("crf").value_or("18").c_str());
        options.includeAudio = !a.flag("no-audio");
        const auto from = parseFrame(a.option("from").value_or("0"), tl.frameRate);
        const auto to = parseFrame(a.option("to").value_or("0"), tl.frameRate);
        if (!from || !to) return usageError("invalid --from/--to");
        options.inFrame = *from;
        options.outFrame = *to;
        render::ExportJob job(s.value()->project(), tl.id, options);
        int lastPercent = -1;
        Status st = job.run([&](const render::ExportProgress& p) {
            const int percent = static_cast<int>(100 * p.framesDone / std::max<FrameIndex>(1, p.framesTotal));
            if (percent != lastPercent && percent % 10 == 0) {
                std::cerr << "\rExporting... " << percent << "%" << std::flush;
                lastPercent = percent;
            }
        });
        std::cerr << "\n";
        if (!st.ok()) return fail(st.error());
        std::cout << "Exported " << options.output.string() << "\n";
        return 0;
    }};

    commands["render-frame"] = {3, [&](const cli::Args&) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        const Timeline& tl = s.value()->timeline();
        const auto at = parseFrame(pos[1], tl.frameRate);
        if (!at) return usageError("invalid position " + pos[1]);
        render::FrameCompositor compositor(render::resolverFor(s.value()->project()));
        auto frame = compositor.render(tl, *at);
        if (!frame.ok()) return fail(frame.error());
        std::ofstream out(pos[2], std::ios::binary);
        out << "P6\n" << frame.value().width << " " << frame.value().height << "\n255\n";
        for (std::size_t i = 0; i < frame.value().pixels.size(); i += 4)
            out.write(reinterpret_cast<const char*>(&frame.value().pixels[i]), 3);
        if (!out) return fail(makeError(ErrorCode::IoError, "cli", "Unable to write " + pos[2]));
        std::cout << "Wrote " << pos[2] << "\n";
        return 0;
    }};

    auto cacheDir = [](const cli::Args& a) -> fs::path {
        if (auto d = a.option("cache-dir")) return *d;
        return DiskCache::defaultDirectory();
    };

    commands["analyze"] = {1, [&](const cli::Args& a) {
        auto s = openProject(pos[0]);
        if (!s.ok()) return fail(s.error());
        const Project& project = s.value()->project();
        MediaAssets assets(cacheDir(a), static_cast<int>(std::max(1u, std::thread::hardware_concurrency() / 2)));
        assets.prefetch(project);
        assets.waitIdle();
        int ready = 0;
        int expected = 0;
        for (const auto& m : project.media) {
            std::cout << (m.online ? "  " : "! ") << m.name << ":";
            if (!m.online) {
                std::cout << " offline, skipped\n";
                continue;
            }
            if (m.info.hasVideo) {
                ++expected;
                const bool ok = assets.thumbnail(m) != nullptr;
                ready += ok;
                std::cout << " thumbnail " << (ok ? "ok" : "FAILED");
            }
            if (m.info.hasAudio) {
                ++expected;
                const auto wave = assets.waveform(m);
                ready += wave != nullptr;
                std::cout << " waveform " << (wave ? std::to_string(wave->peakCount()) + " peaks" : std::string("FAILED"));
            }
            std::cout << "\n";
        }
        std::cout << ready << "/" << expected << " assets ready in " << assets.cache().root().string() << "\n";
        return ready == expected ? 0 : 1;
    }};

    commands["cache-info"] = {0, [&](const cli::Args& a) {
        DiskCache cache(cacheDir(a));
        std::cout << "location=" << cache.root().string() << "\nentries=" << cache.entryCount()
                  << "\nbytes=" << cache.sizeBytes() << "\nlimit=" << cache.maxBytes() << "\n";
        return 0;
    }};

    commands["cache-clear"] = {0, [&](const cli::Args& a) {
        DiskCache cache(cacheDir(a));
        const std::size_t n = cache.entryCount();
        Status st = cache.clear();
        if (!st.ok()) return fail(st.error());
        std::cout << "Removed " << n << " cache entries from " << cache.root().string() << "\n";
        return 0;
    }};

    commands["probe"] = {1, [&](const cli::Args&) {
        auto info = probeMedia(pos[0]);
        if (!info.ok()) return fail(info.error());
        const MediaInfo& i = info.value();
        std::cout << "container=" << i.container << "\nduration=" << i.durationSeconds << "\nvideo=" << i.hasVideo;
        if (i.hasVideo)
            std::cout << "\nvideo_codec=" << i.videoCodec << "\nwidth=" << i.width << "\nheight=" << i.height
                      << "\nframe_rate=" << i.frameRate.toString() << "\npixel_format=" << i.pixelFormat
                      << "\nstill=" << i.isStill;
        std::cout << "\naudio=" << i.hasAudio;
        if (i.hasAudio)
            std::cout << "\naudio_codec=" << i.audioCodec << "\nsample_rate=" << i.sampleRate << "\nchannels=" << i.channels;
        if (!i.timecode.empty()) std::cout << "\ntimecode=" << i.timecode;
        std::cout << "\n";
        return 0;
    }};

    commands["gen-test-media"] = {1, [&](const cli::Args& a) {
        media::SyntheticSpec spec;
        const std::string pattern = a.option("pattern").value_or("bars");
        if (pattern == "solid") spec.pattern = media::SyntheticSpec::Pattern::Solid;
        else if (pattern == "ramp") spec.pattern = media::SyntheticSpec::Pattern::FrameRamp;
        else if (pattern != "bars") return usageError("--pattern must be bars, solid or ramp");
        if (auto c = a.option("color")) {
            int r = 0, g = 0, b = 0;
            if (std::sscanf(c->c_str(), "%d,%d,%d", &r, &g, &b) != 3) return usageError("--color must look like 255,0,0");
            spec.color = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        }
        if (auto f = a.option("frames")) spec.frames = std::atoll(f->c_str());
        if (auto sz = a.option("size"); sz && !parseSize(*sz, spec.width, spec.height))
            return usageError("--size must look like 640x360");
        if (auto f = a.option("fps")) spec.frameRate = Rational::parse(*f);
        if (auto t = a.option("tone")) spec.toneHz = std::atof(t->c_str());
        spec.audio = !a.flag("no-audio");
        Status st = media::generateSyntheticMedia(pos[0], spec);
        if (!st.ok()) return fail(st.error());
        std::cout << "Generated " << pos[0] << "\n";
        return 0;
    }};

    const auto it = commands.find(command);
    if (it == commands.end()) return usageError("unknown command '" + command + "'");
    if (pos.size() < it->second.first) return usageError("'" + command + "' needs more arguments");
    return it->second.second(args);
}
