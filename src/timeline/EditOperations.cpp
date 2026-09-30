#include "timeline/EditOperations.h"

#include <algorithm>

#include "core/Id.h"

namespace up::ops {
namespace {

constexpr const char* kSub = "timeline";

Error notFound(const std::string& what, const std::string& id) {
    return makeError(ErrorCode::NotFound, kSub, "The " + what + " '" + id + "' does not exist in this timeline.",
                     "Refresh the timeline view and try again.");
}

Error outOfRange(const std::string& message, const std::string& suggestion) {
    return makeError(ErrorCode::OutOfRange, kSub, message, suggestion);
}

Error lockedTrack(const Track& track) {
    return makeError(ErrorCode::Locked, kSub, "Track " + track.name + " is locked.",
                     "Unlock the track before editing it.");
}

// Runs `fn` against a copy of the timeline and commits only if it succeeds and
// the result is structurally valid. This makes every operation all-or-nothing.
template <typename R, typename Fn>
R transact(Timeline& timeline, Fn&& fn) {
    Timeline work = timeline;
    R result = fn(work);
    if (!result.ok()) return result;
    Status valid = work.validate();
    if (!valid.ok()) return valid.error();
    timeline = std::move(work);
    return result;
}

void sortClips(Track& track) {
    std::stable_sort(track.clips.begin(), track.clips.end(),
                     [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

std::size_t indexOf(const Track& track, const std::string& clipId) {
    for (std::size_t i = 0; i < track.clips.size(); ++i)
        if (track.clips[i].id == clipId) return i;
    return track.clips.size();
}

// After splitting `left` at right.sourceIn, gives each piece the clip markers on its side.
void partitionMarkers(Clip& left, Clip& right) {
    std::vector<Marker> all = std::move(left.markers);
    left.markers.clear();
    right.markers.clear();
    for (auto& m : all) (m.frame < right.sourceIn ? left.markers : right.markers).push_back(std::move(m));
}

// Splits the clip strictly containing `frame`, if any. The right piece is unlinked.
void splitAt(Track& track, FrameIndex frame) {
    for (std::size_t i = 0; i < track.clips.size(); ++i) {
        Clip& c = track.clips[i];
        if (c.start < frame && frame < c.end()) {
            Clip right = c;
            right.id = generateId();
            right.linkId.clear();
            right.start = frame;
            right.sourceIn += frame - c.start;
            right.duration = c.end() - frame;
            c.duration = frame - c.start;
            partitionMarkers(c, right);
            track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(i + 1), std::move(right));
            return;
        }
    }
}

void clearTrackRange(Track& track, FrameIndex from, FrameIndex to) {
    if (to <= from) return;
    splitAt(track, from);
    splitAt(track, to);
    track.clips.erase(std::remove_if(track.clips.begin(), track.clips.end(),
                                     [&](const Clip& c) { return c.start >= from && c.end() <= to; }),
                      track.clips.end());
}

void shiftFrom(Track& track, FrameIndex from, FrameIndex delta, const std::string& exceptId = {}) {
    for (auto& c : track.clips)
        if (c.start >= from && c.id != exceptId) c.start += delta;
}

Status checkSourceBounds(const Clip& c) {
    if (c.sourceIn < 0) {
        return outOfRange("Cannot extend '" + c.name + "' before the first frame of its media.",
                          "Use a smaller adjustment; there is no more media before this point.");
    }
    if (c.bounded() && c.sourceOut() > c.sourceLength) {
        return outOfRange("Cannot extend '" + c.name + "' past the last frame of its media.",
                          "Use a smaller adjustment; there is no more media after this point.");
    }
    if (c.duration < 1) {
        return outOfRange("The edit would make '" + c.name + "' shorter than one frame.",
                          "Delete the clip instead, or use a smaller adjustment.");
    }
    return Status::success();
}

Result<Track*> editableTrackOfClip(Timeline& tl, const std::string& clipId) {
    Track* track = tl.trackOfClip(clipId);
    if (!track) return notFound("clip", clipId);
    if (track->locked) return lockedTrack(*track);
    return track;
}

Result<Track*> editableTrack(Timeline& tl, const std::string& trackId) {
    Track* track = tl.track(trackId);
    if (!track) return notFound("track", trackId);
    if (track->locked) return lockedTrack(*track);
    return track;
}

}  // namespace

Result<std::string> placeClip(Timeline& timeline, const std::string& trackId, Clip clip, EditMode mode) {
    return transact<Result<std::string>>(timeline, [&](Timeline& tl) -> Result<std::string> {
        auto tr = editableTrack(tl, trackId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        if (clip.start < 0) {
            return outOfRange("Clips cannot be placed before the start of the timeline.",
                              "Place the clip at frame 0 or later.");
        }
        UP_TRY(checkSourceBounds(clip));
        if (clip.id.empty()) clip.id = generateId();
        if (mode == EditMode::Insert) {
            splitAt(track, clip.start);
            shiftFrom(track, clip.start, clip.duration);
        } else {
            clearTrackRange(track, clip.start, clip.end());
        }
        const std::string id = clip.id;
        track.clips.push_back(std::move(clip));
        sortClips(track);
        return id;
    });
}

Result<std::string> appendClip(Timeline& timeline, const std::string& trackId, Clip clip) {
    const Track* track = timeline.track(trackId);
    if (!track) return notFound("track", trackId);
    clip.start = track->end();
    return placeClip(timeline, trackId, std::move(clip), EditMode::Overwrite);
}

Result<std::string> razor(Timeline& timeline, const std::string& clipId, FrameIndex frame,
                          const std::string& rightLinkId) {
    return transact<Result<std::string>>(timeline, [&](Timeline& tl) -> Result<std::string> {
        auto tr = editableTrackOfClip(tl, clipId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        const std::size_t i = indexOf(track, clipId);
        Clip& c = track.clips[i];
        if (frame <= c.start || frame >= c.end()) {
            return outOfRange("The cut point is not inside '" + c.name + "'.",
                              "Move the playhead inside the clip, away from its edges, and cut again.");
        }
        Clip right = c;
        right.id = generateId();
        right.linkId = rightLinkId.empty() ? (c.linkId.empty() ? std::string() : generateId()) : rightLinkId;
        right.start = frame;
        right.sourceIn += frame - c.start;
        right.duration = c.end() - frame;
        c.duration = frame - c.start;
        partitionMarkers(c, right);
        const std::string id = right.id;
        track.clips.insert(track.clips.begin() + static_cast<std::ptrdiff_t>(i + 1), std::move(right));
        return id;
    });
}

Status lift(Timeline& timeline, const std::string& clipId) {
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrackOfClip(tl, clipId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(indexOf(track, clipId)));
        return Status::success();
    });
}

Status rippleDelete(Timeline& timeline, const std::string& clipId) {
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrackOfClip(tl, clipId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        const std::size_t i = indexOf(track, clipId);
        const FrameIndex start = track.clips[i].start;
        const FrameIndex duration = track.clips[i].duration;
        track.clips.erase(track.clips.begin() + static_cast<std::ptrdiff_t>(i));
        shiftFrom(track, start, -duration);
        return Status::success();
    });
}

Status trim(Timeline& timeline, const std::string& clipId, Edge edge, FrameIndex delta, TrimMode mode) {
    if (delta == 0) return Status::success();
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrackOfClip(tl, clipId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        const std::size_t i = indexOf(track, clipId);
        Clip& c = track.clips[i];
        const FrameIndex oldEnd = c.end();
        const Clip* prev = i > 0 ? &track.clips[i - 1] : nullptr;
        const Clip* next = i + 1 < track.clips.size() ? &track.clips[i + 1] : nullptr;

        if (edge == Edge::Out) {
            c.duration += delta;
            UP_TRY(checkSourceBounds(c));
            if (mode == TrimMode::Ripple) {
                shiftFrom(track, oldEnd, delta, c.id);
            } else if (next && c.end() > next->start) {
                return outOfRange("Cannot extend '" + c.name + "' over the next clip.",
                                  "Use a ripple trim or a roll edit, or make room first.");
            }
        } else {
            if (mode == TrimMode::Ripple) {
                // The clip keeps its position; its head is trimmed and later clips follow.
                c.sourceIn += delta;
                c.duration -= delta;
                UP_TRY(checkSourceBounds(c));
                shiftFrom(track, oldEnd, -delta, c.id);
            } else {
                c.start += delta;
                c.sourceIn += delta;
                c.duration -= delta;
                UP_TRY(checkSourceBounds(c));
                if (c.start < 0) {
                    return outOfRange("Cannot extend '" + c.name + "' before the start of the timeline.",
                                      "Use a smaller adjustment.");
                }
                if (prev && c.start < prev->end()) {
                    return outOfRange("Cannot extend '" + c.name + "' over the previous clip.",
                                      "Use a ripple trim or a roll edit, or make room first.");
                }
            }
        }
        sortClips(track);
        return Status::success();
    });
}

Status roll(Timeline& timeline, const std::string& leftClipId, FrameIndex delta) {
    if (delta == 0) return Status::success();
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrackOfClip(tl, leftClipId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        const std::size_t i = indexOf(track, leftClipId);
        if (i + 1 >= track.clips.size() || track.clips[i + 1].start != track.clips[i].end()) {
            return makeError(ErrorCode::InvalidArgument, kSub,
                             "A roll edit needs a clip directly after '" + track.clips[i].name + "'.",
                             "Roll only between two adjacent clips; use trim when there is a gap.");
        }
        Clip& left = track.clips[i];
        Clip& right = track.clips[i + 1];
        left.duration += delta;
        right.start += delta;
        right.sourceIn += delta;
        right.duration -= delta;
        UP_TRY(checkSourceBounds(left));
        UP_TRY(checkSourceBounds(right));
        return Status::success();
    });
}

Status slip(Timeline& timeline, const std::string& clipId, FrameIndex delta) {
    if (delta == 0) return Status::success();
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrackOfClip(tl, clipId);
        if (!tr.ok()) return tr.error();
        Clip& c = *tl.clip(clipId);
        c.sourceIn += delta;
        return checkSourceBounds(c);
    });
}

Status slide(Timeline& timeline, const std::string& clipId, FrameIndex delta) {
    if (delta == 0) return Status::success();
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrackOfClip(tl, clipId);
        if (!tr.ok()) return tr.error();
        Track& track = *tr.value();
        const std::size_t i = indexOf(track, clipId);
        Clip& c = track.clips[i];
        Clip* prev = i > 0 ? &track.clips[i - 1] : nullptr;
        Clip* next = i + 1 < track.clips.size() ? &track.clips[i + 1] : nullptr;
        const bool leftAdjacent = prev && prev->end() == c.start;
        const bool rightAdjacent = next && next->start == c.end();

        c.start += delta;
        if (c.start < 0) {
            return outOfRange("Cannot slide '" + c.name + "' before the start of the timeline.",
                              "Use a smaller adjustment.");
        }
        if (leftAdjacent) {
            prev->duration += delta;
            UP_TRY(checkSourceBounds(*prev));
        } else if (prev && c.start < prev->end()) {
            return outOfRange("Cannot slide '" + c.name + "' over the previous clip.", "Use a smaller adjustment.");
        }
        if (rightAdjacent) {
            next->start += delta;
            next->sourceIn += delta;
            next->duration -= delta;
            UP_TRY(checkSourceBounds(*next));
        } else if (next && c.end() > next->start) {
            return outOfRange("Cannot slide '" + c.name + "' over the next clip.", "Use a smaller adjustment.");
        }
        return Status::success();
    });
}

Status moveClip(Timeline& timeline, const std::string& clipId, const std::string& targetTrackId,
                FrameIndex newStart) {
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto src = editableTrackOfClip(tl, clipId);
        if (!src.ok()) return src.error();
        auto dst = editableTrack(tl, targetTrackId);
        if (!dst.ok()) return dst.error();
        Track& from = *src.value();
        Track& to = *dst.value();
        if (from.kind != to.kind) {
            return makeError(ErrorCode::InvalidArgument, kSub,
                             std::string("Cannot move a ") + toString(from.kind) + " clip onto " + toString(to.kind) +
                                 " track " + to.name + ".",
                             "Drop the clip on a track of the same kind.");
        }
        if (newStart < 0) {
            return outOfRange("Clips cannot be moved before the start of the timeline.",
                              "Move the clip to frame 0 or later.");
        }
        const std::size_t i = indexOf(from, clipId);
        Clip c = from.clips[i];
        from.clips.erase(from.clips.begin() + static_cast<std::ptrdiff_t>(i));
        c.start = newStart;
        clearTrackRange(to, c.start, c.end());
        to.clips.push_back(std::move(c));
        sortClips(to);
        return Status::success();
    });
}

Status clearRange(Timeline& timeline, const std::string& trackId, FrameIndex from, FrameIndex to) {
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrack(tl, trackId);
        if (!tr.ok()) return tr.error();
        clearTrackRange(*tr.value(), from, to);
        return Status::success();
    });
}

Status insertGap(Timeline& timeline, const std::string& trackId, FrameIndex at, FrameIndex length) {
    if (length <= 0) {
        return makeError(ErrorCode::InvalidArgument, kSub, "A gap must be at least one frame long.");
    }
    return transact<Status>(timeline, [&](Timeline& tl) -> Status {
        auto tr = editableTrack(tl, trackId);
        if (!tr.ok()) return tr.error();
        splitAt(*tr.value(), at);
        shiftFrom(*tr.value(), at, length);
        return Status::success();
    });
}

}  // namespace up::ops
