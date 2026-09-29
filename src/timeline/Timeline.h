#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/Rational.h"
#include "core/Result.h"

namespace up {

enum class TrackKind { Video, Audio };

const char* toString(TrackKind kind);

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

    FrameIndex end() const { return start + duration; }
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

    // Verifies structural invariants (sorted, non-overlapping, positive durations,
    // source ranges in bounds, unique ids, marks ordered, targets of the right kind).
    // Used by tests and after loading.
    Status validate() const;
};

}  // namespace up
