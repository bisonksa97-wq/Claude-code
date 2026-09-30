#pragma once

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Result.h"

namespace up::audio {

// A parameter of an effect type, for validation and for building editors.
struct EffectParamInfo {
    std::string id;
    std::string label;
    std::string unit;
    double defaultValue;
    double minimum;
    double maximum;
};

struct EffectTypeInfo {
    std::string type;   // stable id stored in projects, e.g. "eq3"
    std::string label;  // shown in the UI
    std::vector<EffectParamInfo> params;
};

// Built-in effect types, in menu order.
const std::vector<EffectTypeInfo>& effectTypes();
const EffectTypeInfo* effectType(const std::string& type);

// An effect instance as stored on a track. Missing parameters take their defaults.
struct EffectSpec {
    std::string id;
    std::string type;
    bool enabled = true;
    std::map<std::string, double> params;

    double param(const std::string& name) const;
};

// Creates a spec of `type` with default parameters and a fresh id.
Result<EffectSpec> makeEffect(const std::string& type);
// Checks the type exists and every parameter is known and in range.
Status validateEffect(const EffectSpec& spec);

// Real-time processor for interleaved float audio. Keeps state between calls
// (filters, envelopes); `reset` clears it (after a seek).
class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void prepare(int sampleRate, int channels) = 0;
    virtual void reset() = 0;
    virtual void process(float* interleaved, int64_t frames) = 0;
};

Result<std::unique_ptr<AudioProcessor>> createProcessor(const EffectSpec& spec);

// Stereo balance law with unity at centre: the far side is attenuated along a
// constant-power curve and the near side never exceeds unity.
// `pan` from -1 (left) to 1 (right). Returns {left gain, right gain}.
std::pair<float, float> panGains(double pan);

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

}  // namespace up::audio
