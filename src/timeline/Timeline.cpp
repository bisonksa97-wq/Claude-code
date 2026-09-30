#include "timeline/Timeline.h"

#include <algorithm>
#include <array>
#include <set>

#include "core/Id.h"

namespace up {

const char* toString(TrackKind kind) {
    return kind == TrackKind::Video ? "video" : "audio";
}

namespace {
constexpr std::array<const char*, 6> kMarkerColorNames{"red", "orange", "yellow", "green", "blue", "purple"};
}  // namespace

const char* toString(MarkerColor color) { return kMarkerColorNames[static_cast<std::size_t>(color)]; }

std::optional<MarkerColor> markerColorFromString(const std::string& name) {
    for (std::size_t i = 0; i < kMarkerColorNames.size(); ++i)
        if (name == kMarkerColorNames[i]) return static_cast<MarkerColor>(i);
    return std::nullopt;
}

const Clip* Track::clipAt(FrameIndex frame) const {
    auto it = std::upper_bound(clips.begin(), clips.end(), frame,
                               [](FrameIndex f, const Clip& c) { return f < c.start; });
    if (it == clips.begin()) return nullptr;
    --it;
    return it->contains(frame) ? &*it : nullptr;
}

FrameIndex Track::end() const {
    return clips.empty() ? 0 : clips.back().end();
}

Timeline Timeline::create(std::string name, FrameRate rate, int width, int height, int sampleRate,
                          int videoTracks, int audioTracks) {
    Timeline t;
    t.id = generateId();
    t.name = std::move(name);
    t.frameRate = rate;
    t.width = width;
    t.height = height;
    t.sampleRate = sampleRate;
    for (int i = 0; i < videoTracks; ++i) t.addTrack(TrackKind::Video);
    for (int i = 0; i < audioTracks; ++i) t.addTrack(TrackKind::Audio);
    if (const auto v = t.trackIdsOfKind(TrackKind::Video); !v.empty()) t.videoTarget = v.front();
    if (const auto a = t.trackIdsOfKind(TrackKind::Audio); !a.empty()) t.audioTarget = a.front();
    return t;
}

Track& Timeline::addTrack(TrackKind kind) {
    Track tr;
    tr.id = generateId();
    tr.kind = kind;
    // First free "V<n>"/"A<n>" name, so names stay unique after tracks are removed.
    const std::string prefix = kind == TrackKind::Video ? "V" : "A";
    for (int n = 1;; ++n) {
        const std::string candidate = prefix + std::to_string(n);
        if (std::none_of(tracks.begin(), tracks.end(), [&](const Track& t) { return t.name == candidate; })) {
            tr.name = candidate;
            break;
        }
    }
    tracks.push_back(std::move(tr));
    return tracks.back();
}

Track* Timeline::track(const std::string& trackId) {
    for (auto& t : tracks)
        if (t.id == trackId) return &t;
    return nullptr;
}

const Track* Timeline::track(const std::string& trackId) const {
    return const_cast<Timeline*>(this)->track(trackId);
}

Track* Timeline::trackOfClip(const std::string& clipId) {
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (c.id == clipId) return &t;
    return nullptr;
}

const Track* Timeline::trackOfClip(const std::string& clipId) const {
    return const_cast<Timeline*>(this)->trackOfClip(clipId);
}

Clip* Timeline::clip(const std::string& clipId) {
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (c.id == clipId) return &c;
    return nullptr;
}

const Clip* Timeline::clip(const std::string& clipId) const {
    return const_cast<Timeline*>(this)->clip(clipId);
}

std::vector<const Track*> Timeline::tracksOfKind(TrackKind kind) const {
    std::vector<const Track*> out;
    for (const auto& t : tracks)
        if (t.kind == kind) out.push_back(&t);
    return out;
}

std::vector<std::string> Timeline::trackIdsOfKind(TrackKind kind) const {
    std::vector<std::string> out;
    for (const auto& t : tracks)
        if (t.kind == kind) out.push_back(t.id);
    return out;
}

std::vector<std::string> Timeline::linkedClips(const std::string& clipId) const {
    std::vector<std::string> out;
    const Clip* c = clip(clipId);
    if (!c || c->linkId.empty()) return out;
    for (const auto& t : tracks)
        for (const auto& other : t.clips)
            if (other.linkId == c->linkId && other.id != clipId) out.push_back(other.id);
    return out;
}

FrameIndex Timeline::duration() const {
    FrameIndex d = 0;
    for (const auto& t : tracks) d = std::max(d, t.end());
    return d;
}

std::vector<FrameIndex> Timeline::markerPositions() const {
    std::vector<FrameIndex> out;
    for (const auto& m : markers) out.push_back(m.frame);
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            for (const auto& m : c.markers) {
                const FrameIndex f = c.toTimeline(m.frame);
                if (c.contains(f)) out.push_back(f);
            }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::optional<FrameIndex> Timeline::nextMarker(FrameIndex after) const {
    for (FrameIndex f : markerPositions())
        if (f > after) return f;
    return std::nullopt;
}

std::optional<FrameIndex> Timeline::previousMarker(FrameIndex before) const {
    const auto positions = markerPositions();
    for (auto it = positions.rbegin(); it != positions.rend(); ++it)
        if (*it < before) return *it;
    return std::nullopt;
}

Status Timeline::validate() const {
    auto fail = [](const std::string& msg) {
        return makeError(ErrorCode::Internal, "timeline", "The timeline structure is inconsistent: " + msg,
                         "Undo the last operation and report this problem.");
    };
    if (!frameRate.valid()) return fail("invalid frame rate " + frameRate.toString());
    if ((markIn && *markIn < 0) || (markIn && markOut && *markOut <= *markIn)) return fail("marks out of order");
    auto checkTarget = [&](const std::string& trackId, TrackKind kind) {
        if (trackId.empty()) return true;
        const Track* t = track(trackId);
        return t && t->kind == kind;
    };
    if (!checkTarget(videoTarget, TrackKind::Video) || !checkTarget(audioTarget, TrackKind::Audio))
        return fail("a source target refers to a missing track or one of the wrong kind");
    std::set<std::string> ids;
    auto checkMarkers = [&](const std::vector<Marker>& list, bool sorted) -> bool {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (!ids.insert(list[i].id).second || list[i].duration < 0 || (sorted && list[i].frame < 0)) return false;
            if (sorted && i > 0 && list[i - 1].frame > list[i].frame) return false;
        }
        return true;
    };
    if (!checkMarkers(markers, true)) return fail("invalid or unsorted timeline markers");
    for (const auto& t : tracks) {
        if (!ids.insert(t.id).second) return fail("duplicate track id " + t.id);
        for (std::size_t i = 0; i < t.clips.size(); ++i) {
            const Clip& c = t.clips[i];
            if (!ids.insert(c.id).second) return fail("duplicate clip id " + c.id);
            if (c.duration <= 0) return fail("clip " + c.id + " has non-positive duration");
            if (c.start < 0) return fail("clip " + c.id + " starts before 0");
            if (c.sourceIn < 0) return fail("clip " + c.id + " has negative source in");
            if (c.bounded() && c.sourceOut() > c.sourceLength) return fail("clip " + c.id + " exceeds its media");
            if (i > 0 && t.clips[i - 1].end() > c.start) return fail("clips overlap on track " + t.name);
            if (!checkMarkers(c.markers, false)) return fail("invalid markers on clip " + c.id);
        }
    }
    return Status::success();
}

}  // namespace up
