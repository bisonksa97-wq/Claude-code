#include "audio/AudioEffect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

#include "core/Id.h"

namespace up::audio {
namespace {

constexpr double kPi = 3.14159265358979323846;

// --- Gain -------------------------------------------------------------------------------

class GainProcessor final : public AudioProcessor {
public:
    explicit GainProcessor(const EffectSpec& spec) : gain_(static_cast<float>(dbToGain(spec.param("gain")))) {}
    void prepare(int, int channels) override { channels_ = channels; }
    void reset() override {}
    void process(float* data, int64_t frames) override {
        for (int64_t i = 0; i < frames * channels_; ++i) data[i] *= gain_;
    }

private:
    float gain_;
    int channels_ = 2;
};

// --- Biquad (RBJ Audio EQ Cookbook) ---------------------------------------------------------

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 8> z1{}, z2{};  // per-channel state (transposed direct form II)

    static Biquad lowShelf(double rate, double freq, double gainDb) { return shelf(rate, freq, gainDb, true); }
    static Biquad highShelf(double rate, double freq, double gainDb) { return shelf(rate, freq, gainDb, false); }

    static Biquad peaking(double rate, double freq, double gainDb, double q) {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * freq / rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double cw = std::cos(w);
        return normalized(1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    static Biquad shelf(double rate, double freq, double gainDb, bool low) {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * freq / rate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / 2.0 * std::sqrt(2.0);  // shelf slope S = 1
        const double sa = 2.0 * std::sqrt(A) * alpha;
        if (low) {
            return normalized(A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw),
                              A * ((A + 1) - (A - 1) * cw - sa), (A + 1) + (A - 1) * cw + sa,
                              -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa);
        }
        return normalized(A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw),
                          A * ((A + 1) + (A - 1) * cw - sa), (A + 1) - (A - 1) * cw + sa,
                          2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa);
    }

    static Biquad normalized(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        Biquad q;
        q.b0 = nb0 / na0;
        q.b1 = nb1 / na0;
        q.b2 = nb2 / na0;
        q.a1 = na1 / na0;
        q.a2 = na2 / na0;
        return q;
    }

    inline float tick(float x, int ch) {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
    void clear() {
        z1.fill(0);
        z2.fill(0);
    }
};

class Eq3Processor final : public AudioProcessor {
public:
    explicit Eq3Processor(const EffectSpec& spec) : spec_(spec) {}
    void prepare(int sampleRate, int channels) override {
        channels_ = std::min(channels, 8);
        const double nyquistSafe = sampleRate * 0.45;
        low_ = Biquad::lowShelf(sampleRate, std::min(spec_.param("lowFreq"), nyquistSafe), spec_.param("lowGain"));
        mid_ = Biquad::peaking(sampleRate, std::min(spec_.param("midFreq"), nyquistSafe), spec_.param("midGain"),
                               spec_.param("midQ"));
        high_ = Biquad::highShelf(sampleRate, std::min(spec_.param("highFreq"), nyquistSafe), spec_.param("highGain"));
    }
    void reset() override {
        low_.clear();
        mid_.clear();
        high_.clear();
    }
    void process(float* data, int64_t frames) override {
        for (int64_t i = 0; i < frames; ++i)
            for (int c = 0; c < channels_; ++c) {
                float& s = data[i * channels_ + c];
                s = high_.tick(mid_.tick(low_.tick(s, c), c), c);
            }
    }

private:
    EffectSpec spec_;
    int channels_ = 2;
    Biquad low_, mid_, high_;
};

// --- Compressor -----------------------------------------------------------------------------
// Feed-forward, stereo-linked peak detector with attack/release smoothing of the gain
// reduction (in dB), hard knee, and make-up gain.
class CompressorProcessor final : public AudioProcessor {
public:
    explicit CompressorProcessor(const EffectSpec& spec)
        : threshold_(spec.param("threshold")), ratio_(spec.param("ratio")), attackMs_(spec.param("attack")),
          releaseMs_(spec.param("release")), makeup_(spec.param("makeup")) {}
    void prepare(int sampleRate, int channels) override {
        channels_ = channels;
        attackCoeff_ = std::exp(-1.0 / (std::max(0.01, attackMs_) * 0.001 * sampleRate));
        releaseCoeff_ = std::exp(-1.0 / (std::max(0.01, releaseMs_) * 0.001 * sampleRate));
        reset();
    }
    void reset() override { reductionDb_ = 0.0; }
    void process(float* data, int64_t frames) override {
        for (int64_t i = 0; i < frames; ++i) {
            float peak = 0.0f;
            for (int c = 0; c < channels_; ++c) peak = std::max(peak, std::abs(data[i * channels_ + c]));
            const double levelDb = 20.0 * std::log10(std::max(1e-9, static_cast<double>(peak)));
            const double over = levelDb - threshold_;
            const double target = over > 0.0 ? over - over / ratio_ : 0.0;  // dB of reduction wanted
            const double coeff = target > reductionDb_ ? attackCoeff_ : releaseCoeff_;
            reductionDb_ = target + (reductionDb_ - target) * coeff;
            const auto g = static_cast<float>(dbToGain(makeup_ - reductionDb_));
            for (int c = 0; c < channels_; ++c) data[i * channels_ + c] *= g;
        }
    }

private:
    double threshold_, ratio_, attackMs_, releaseMs_, makeup_;
    double attackCoeff_ = 0.0, releaseCoeff_ = 0.0, reductionDb_ = 0.0;
    int channels_ = 2;
};

}  // namespace

const std::vector<EffectTypeInfo>& effectTypes() {
    static const std::vector<EffectTypeInfo> types = {
        {"gain", "Gain", {{"gain", "Gain", "dB", 0.0, -60.0, 24.0}}},
        {"eq3",
         "3-Band EQ",
         {{"lowFreq", "Low Frequency", "Hz", 120.0, 20.0, 1000.0},
          {"lowGain", "Low Gain", "dB", 0.0, -24.0, 24.0},
          {"midFreq", "Mid Frequency", "Hz", 1000.0, 100.0, 10000.0},
          {"midGain", "Mid Gain", "dB", 0.0, -24.0, 24.0},
          {"midQ", "Mid Q", "", 0.7, 0.1, 10.0},
          {"highFreq", "High Frequency", "Hz", 8000.0, 1000.0, 20000.0},
          {"highGain", "High Gain", "dB", 0.0, -24.0, 24.0}}},
        {"compressor",
         "Compressor",
         {{"threshold", "Threshold", "dB", -18.0, -60.0, 0.0},
          {"ratio", "Ratio", ":1", 4.0, 1.0, 20.0},
          {"attack", "Attack", "ms", 10.0, 0.1, 200.0},
          {"release", "Release", "ms", 100.0, 5.0, 2000.0},
          {"makeup", "Make-up Gain", "dB", 0.0, 0.0, 24.0}}},
    };
    return types;
}

const EffectTypeInfo* effectType(const std::string& type) {
    for (const auto& t : effectTypes())
        if (t.type == type) return &t;
    return nullptr;
}

double EffectSpec::param(const std::string& name) const {
    if (auto it = params.find(name); it != params.end()) return it->second;
    if (const EffectTypeInfo* info = effectType(type))
        for (const auto& p : info->params)
            if (p.id == name) return p.defaultValue;
    return 0.0;
}

Result<EffectSpec> makeEffect(const std::string& type) {
    const EffectTypeInfo* info = effectType(type);
    if (!info) {
        return makeError(ErrorCode::NotFound, "audio", "There is no audio effect called '" + type + "'.",
                         "Use one of: gain, eq3, compressor.");
    }
    EffectSpec spec;
    spec.id = generateId();
    spec.type = type;
    for (const auto& p : info->params) spec.params[p.id] = p.defaultValue;
    return spec;
}

Status validateEffect(const EffectSpec& spec) {
    const EffectTypeInfo* info = effectType(spec.type);
    if (!info) return makeError(ErrorCode::NotFound, "audio", "Unknown audio effect '" + spec.type + "'.");
    for (const auto& [name, value] : spec.params) {
        auto it = std::find_if(info->params.begin(), info->params.end(), [&](const EffectParamInfo& p) { return p.id == name; });
        if (it == info->params.end()) {
            return makeError(ErrorCode::InvalidArgument, "audio", info->label + " has no parameter '" + name + "'.");
        }
        if (!std::isfinite(value) || value < it->minimum || value > it->maximum) {
            auto number = [](double v) {
                std::ostringstream os;
                os << v;  // shortest natural form: 1, 20, 0.1
                return os.str();
            };
            return makeError(ErrorCode::OutOfRange, "audio",
                             info->label + " " + it->label + " must be between " + number(it->minimum) + " and " +
                                 number(it->maximum) + (it->unit.empty() ? "" : " " + it->unit) + ".");
        }
    }
    return Status::success();
}

Result<std::unique_ptr<AudioProcessor>> createProcessor(const EffectSpec& spec) {
    UP_TRY(validateEffect(spec));
    if (spec.type == "gain") return std::unique_ptr<AudioProcessor>(new GainProcessor(spec));
    if (spec.type == "eq3") return std::unique_ptr<AudioProcessor>(new Eq3Processor(spec));
    return std::unique_ptr<AudioProcessor>(new CompressorProcessor(spec));
}

std::pair<float, float> panGains(double pan) {
    pan = std::clamp(pan, -1.0, 1.0);
    const double angle = (pan + 1.0) * kPi / 4.0;  // 0 .. pi/2
    const double left = std::min(1.0, std::sqrt(2.0) * std::cos(angle));
    const double right = std::min(1.0, std::sqrt(2.0) * std::sin(angle));
    return {static_cast<float>(left), static_cast<float>(right)};
}

}  // namespace up::audio
