#include "timeline/Animation.h"

#include <algorithm>
#include <cmath>

namespace up {

const char* toString(Interpolation interpolation) {
    switch (interpolation) {
        case Interpolation::Linear: return "linear";
        case Interpolation::Hold: return "hold";
        case Interpolation::EaseInOut: return "ease";
    }
    return "linear";
}

std::optional<Interpolation> interpolationFromString(const std::string& name) {
    for (auto i : {Interpolation::Linear, Interpolation::Hold, Interpolation::EaseInOut})
        if (name == toString(i)) return i;
    return std::nullopt;
}

double AnimatedValue::at(FrameIndex frame) const {
    if (keys.empty()) return value;
    if (frame <= keys.front().frame) return keys.front().value;
    if (frame >= keys.back().frame) return keys.back().value;
    auto next = std::upper_bound(keys.begin(), keys.end(), frame,
                                 [](FrameIndex f, const Keyframe& k) { return f < k.frame; });
    const Keyframe& b = *next;
    const Keyframe& a = *(next - 1);
    const double t = static_cast<double>(frame - a.frame) / static_cast<double>(b.frame - a.frame);
    switch (a.interpolation) {
        case Interpolation::Hold: return a.value;
        case Interpolation::EaseInOut: {
            const double s = t * t * (3.0 - 2.0 * t);  // smoothstep: zero velocity at both keys
            return a.value + (b.value - a.value) * s;
        }
        case Interpolation::Linear:
        default: return a.value + (b.value - a.value) * t;
    }
}

const Keyframe* AnimatedValue::keyAt(FrameIndex frame) const {
    for (const auto& k : keys)
        if (k.frame == frame) return &k;
    return nullptr;
}

void AnimatedValue::setKey(FrameIndex frame, double v, std::optional<Interpolation> interpolation) {
    for (auto& k : keys) {
        if (k.frame == frame) {
            k.value = v;
            if (interpolation) k.interpolation = *interpolation;
            return;
        }
    }
    Keyframe k{frame, v, interpolation.value_or(Interpolation::Linear)};
    keys.insert(std::upper_bound(keys.begin(), keys.end(), frame,
                                 [](FrameIndex f, const Keyframe& key) { return f < key.frame; }),
                k);
}

bool AnimatedValue::removeKey(FrameIndex frame) {
    const auto before = keys.size();
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const Keyframe& k) { return k.frame == frame; }), keys.end());
    return keys.size() != before;
}

const ClipParamInfo& paramInfo(ClipParam param) {
    static const std::array<ClipParamInfo, kAllClipParams.size()> info = {{
        {"positionX", "Position X", "px", 0.0, -100000.0, 100000.0},
        {"positionY", "Position Y", "px", 0.0, -100000.0, 100000.0},
        {"scale", "Scale", "%", 100.0, 0.0, 10000.0},
        {"rotation", "Rotation", "°", 0.0, -36000.0, 36000.0},
        {"opacity", "Opacity", "%", 100.0, 0.0, 100.0},
        {"cropLeft", "Crop Left", "%", 0.0, 0.0, 100.0},
        {"cropRight", "Crop Right", "%", 0.0, 0.0, 100.0},
        {"cropTop", "Crop Top", "%", 0.0, 0.0, 100.0},
        {"cropBottom", "Crop Bottom", "%", 0.0, 0.0, 100.0},
    }};
    return info[static_cast<std::size_t>(param)];
}

std::optional<ClipParam> clipParamFromString(const std::string& id) {
    for (ClipParam p : kAllClipParams)
        if (id == paramInfo(p).id) return p;
    return std::nullopt;
}

std::array<AnimatedValue, kAllClipParams.size()> ClipTransform::defaults() {
    std::array<AnimatedValue, kAllClipParams.size()> out{};
    for (ClipParam p : kAllClipParams) out[static_cast<std::size_t>(p)].value = paramInfo(p).defaultValue;
    return out;
}

bool ClipTransform::isIdentity() const {
    for (ClipParam p : kAllClipParams) {
        const AnimatedValue& v = (*this)[p];
        if (v.animated() || v.value != paramInfo(p).defaultValue) return false;
    }
    return true;
}

}  // namespace up
