#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/Rational.h"
#include "core/Result.h"
#include "timeline/Animation.h"

namespace up {

enum class TrackKind { Video, Audio };

const char* toString(TrackKind kind);

enum class MarkerColor { Red, Orange, Yellow, Green, Blue, Purple };

const char* toString(MarkerColor color);
std::optional<MarkerColor> markerColorFromString(const std::string& name);

// A named point (or range, when duration > 0) with a comment.
// Timeline markers are positioned in timeline frames and do not move with edits.
// Clip markers are positioned in *source* frames (timeline rate), so they stay on
// the same picture when the clip is moved, trimmed, slipped or cut.
struct Marker {
    std::string id;
    FrameIndex frame = 0;
    FrameIndex duration = 0;
    std::string name;
    std::string comment;
    MarkerColor color = MarkerColor::Blue;
};

// A clip places a range of a media item on a track.
//
// All positions are in frames of the owning timeline's frame rate:
//   timeline range  = [start, start + duration)
//   source range    = [sourceIn, sourceIn + duration)   (frame 0 = start of the media)
// `sourceLength` is the usable media length in timeline frames and bounds
// trims/slips; 0 means unbounded (still images, generators).
struct Clip {
    std::string id;
    std::string mediaId;
    std::string name;
    FrameIndex start = 0;
    FrameIndex duration = 0;
    FrameIndex sourceIn = 0;
    FrameIndex sourceLength = 0;
    // Clips sharing a non-empty linkId (e.g. the video and audio of one file) are edited together.
    std::string linkId;
    bool enabled = true;
    double gainDb = 0.0;  // audio clips only
    std::vector<Marker> markers;  // source-frame positions
    ClipTransform transform;      // video clips only; keyframes in source frames

    FrameIndex end() const { return start + duration; }
    // Timeline frame of a source frame of this clip (may fall outside the clip).
    FrameIndex toTimeline(FrameIndex sourceFrame) const { return start + (sourceFrame - sourceIn); }
    FrameIndex toSource(FrameIndex timelineFrame) const { return sourceIn + (timelineFrame - start); }
    FrameIndex sourceOut() const { return sourceIn + duration; }
    bool contains(FrameIndex frame) const { return frame >= start && frame < end(); }
    bool bounded() const { return sourceLength > 0; }
};

struct Track {
    std::string id;
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool enabled = true;  // disabled tracks are not rendered
    bool locked = false;  // locked tracks reject edits
    bool muted = false;   // audio only
    bool solo = false;    // audio only
    double gainDb = 0.0;  // audio only
    // Sorted by start; clips never overlap (enforced by the edit operations).
    std::vector<Clip> clips;

    const Clip* clipAt(FrameIndex frame) const;
    FrameIndex end() const;
};

class Timeline {
public:
    std::string id;
    std::string name;
    FrameRate frameRate{25, 1};
    int width = 1920;
    int height = 1080;
    int sampleRate = 48000;
    std::vector<Track> tracks;

    // Record in/out marks for three-point editing ([markIn, markOut), frames).
    std::optional<FrameIndex> markIn;
    std::optional<FrameIndex> markOut;
    // Source patching: the tracks that receive a source's video/audio in
    // three-point edits. Empty = that stream is not edited in.
    std::string videoTarget;
    std::string audioTarget;
    std::vector<Marker> markers;  // sorted by frame

    // Creates a timeline with `videoTracks` video and `audioTracks` audio tracks.
    static Timeline create(std::string name, FrameRate rate, int width, int height, int sampleRate,
                           int videoTracks = 2, int audioTracks = 2);

    Track& addTrack(TrackKind kind);

    Track* track(const std::string& trackId);
    const Track* track(const std::string& trackId) const;
    Track* trackOfClip(const std::string& clipId);
    const Track* trackOfClip(const std::string& clipId) const;
    Clip* clip(const std::string& clipId);
    const Clip* clip(const std::string& clipId) const;

    // Tracks of one kind in stacking order (V1 first; higher tracks draw on top).
    std::vector<const Track*> tracksOfKind(TrackKind kind) const;
    std::vector<std::string> trackIdsOfKind(TrackKind kind) const;

    // Ids of the other clips sharing `clipId`'s link group.
    std::vector<std::string> linkedClips(const std::string& clipId) const;

    // End of the last clip on any track.
    FrameIndex duration() const;

    // Timeline positions of all markers: timeline markers plus clip markers that fall
    // inside their clip's visible range. Sorted, without duplicates.
    std::vector<FrameIndex> markerPositions() const;
    std::optional<FrameIndex> nextMarker(FrameIndex after) const;
    std::optional<FrameIndex> previousMarker(FrameIndex before) const;

    // Verifies structural invariants (sorted, non-overlapping, positive durations,
    // source ranges in bounds, unique ids, marks ordered, targets of the right kind).
    // Used by tests and after loading.
    Status validate() const;
};

}  // namespace up
