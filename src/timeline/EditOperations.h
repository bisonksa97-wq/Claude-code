#pragma once

#include <string>

#include "core/Result.h"
#include "timeline/Timeline.h"

// Deterministic, frame-accurate timeline edit operations.
//
// Every operation either succeeds and leaves the timeline valid, or fails and
// leaves it unchanged. Operations never touch linked partners: link-aware
// behaviour is composed at the application-service level.
namespace up::ops {

enum class EditMode {
    Overwrite,  // replaces whatever occupies the destination range
    Insert,     // pushes later material to the right (ripple)
};

enum class TrimMode {
    Normal,  // edge moves; neighbours and timeline length unaffected (gaps may open/close)
    Ripple,  // later clips on the track shift to follow the edit
};

enum class Edge { In, Out };

// Places `clip` on `trackId` at `clip.start`. A new id is assigned when `clip.id` is empty.
// Returns the id of the placed clip.
Result<std::string> placeClip(Timeline& timeline, const std::string& trackId, Clip clip, EditMode mode);

// Places `clip` directly after the last clip on the track.
Result<std::string> appendClip(Timeline& timeline, const std::string& trackId, Clip clip);

// Splits a clip in two at `frame` (which must be strictly inside the clip).
// Returns the id of the new right-hand clip. Linked right-hand pieces share a
// fresh link id derived from `rightLinkId` when provided.
Result<std::string> razor(Timeline& timeline, const std::string& clipId, FrameIndex frame,
                          const std::string& rightLinkId = {});

// Removes a clip, leaving a gap.
Status lift(Timeline& timeline, const std::string& clipId);

// Removes a clip and closes the gap by shifting later clips on the same track left.
Status rippleDelete(Timeline& timeline, const std::string& clipId);

// Moves one edge of a clip by `delta` frames (positive = later in time).
Status trim(Timeline& timeline, const std::string& clipId, Edge edge, FrameIndex delta, TrimMode mode);

// Moves the edit point between `leftClipId` and the clip immediately after it by `delta`.
Status roll(Timeline& timeline, const std::string& leftClipId, FrameIndex delta);

// Shifts the clip's source range by `delta` without changing its position or length.
Status slip(Timeline& timeline, const std::string& clipId, FrameIndex delta);

// Moves the clip by `delta`, trimming the adjacent clips so the overall track length is unchanged.
Status slide(Timeline& timeline, const std::string& clipId, FrameIndex delta);

// Moves a clip to `newStart` on `targetTrackId` (same kind), overwriting what is there.
Status moveClip(Timeline& timeline, const std::string& clipId, const std::string& targetTrackId,
                FrameIndex newStart);

// Opens a gap of `length` frames at `at`, splitting a clip that straddles `at`.
Status insertGap(Timeline& timeline, const std::string& trackId, FrameIndex at, FrameIndex length);

// Removes everything in [from, to) on the track, splitting clips that straddle the range.
Status clearRange(Timeline& timeline, const std::string& trackId, FrameIndex from, FrameIndex to);

}  // namespace up::ops
