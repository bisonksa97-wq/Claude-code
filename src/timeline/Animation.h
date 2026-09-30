#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Rational.h"

namespace up {

// Keyframe interpolation towards the next keyframe.
enum class Interpolation { Linear, Hold, EaseInOut };

const char* toString(Interpolation interpolation);
std::optional<Interpolation> interpolationFromString(const std::string& name);

struct Keyframe {
    FrameIndex frame = 0;  // source frame (timeline rate), like clip markers
    double value = 0.0;
    Interpolation interpolation = Interpolation::Linear;
};

// A parameter that is either a constant or a keyframed curve.
// Keyframes are kept sorted by frame with at most one key per frame.
struct AnimatedValue {
    double value = 0.0;           // used when there are no keyframes
    std::vector<Keyframe> keys;   // sorted

    bool animated() const { return !keys.empty(); }
    // Value at a source frame: constant before the first key and after the last.
    double at(FrameIndex frame) const;
    const Keyframe* keyAt(FrameIndex frame) const;
    // Inserts or replaces the key at `frame`.
    void setKey(FrameIndex frame, double v, std::optional<Interpolation> interpolation = std::nullopt);
    bool removeKey(FrameIndex frame);
};

// Animatable video clip properties. Positions are offsets from the frame centre in
// timeline pixels; scale and opacity are percentages; rotation is in degrees
// (clockwise); crops are percentages of the source width/height removed from each edge.
enum class ClipParam { PositionX, PositionY, Scale, Rotation, Opacity, CropLeft, CropRight, CropTop, CropBottom };

inline constexpr std::array<ClipParam, 9> kAllClipParams = {
    ClipParam::PositionX, ClipParam::PositionY, ClipParam::Scale,     ClipParam::Rotation, ClipParam::Opacity,
    ClipParam::CropLeft,  ClipParam::CropRight, ClipParam::CropTop,   ClipParam::CropBottom};

struct ClipParamInfo {
    const char* id;     // stable identifier used in files and the CLI, e.g. "positionX"
    const char* label;  // UI label
    const char* unit;
    double defaultValue;
    double minimum;
    double maximum;
};

const ClipParamInfo& paramInfo(ClipParam param);
std::optional<ClipParam> clipParamFromString(const std::string& id);

struct ClipTransform {
    std::array<AnimatedValue, kAllClipParams.size()> values = defaults();

    AnimatedValue& operator[](ClipParam p) { return values[static_cast<std::size_t>(p)]; }
    const AnimatedValue& operator[](ClipParam p) const { return values[static_cast<std::size_t>(p)]; }
    // All parameters at their defaults with no keyframes.
    bool isIdentity() const;
    static std::array<AnimatedValue, kAllClipParams.size()> defaults();
};

}  // namespace up
