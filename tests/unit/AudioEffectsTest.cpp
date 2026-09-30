#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio/AudioEffect.h"

using namespace up;
using namespace up::audio;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;

std::vector<float> sine(double hz, double amplitude, int frames) {
    std::vector<float> out(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const auto v = static_cast<float>(amplitude * std::sin(2 * kPi * hz * i / kRate));
        out[static_cast<std::size_t>(i) * 2] = v;
        out[static_cast<std::size_t>(i) * 2 + 1] = v;
    }
    return out;
}

double rms(const std::vector<float>& s, std::size_t fromFrame) {
    double sum = 0;
    std::size_t n = 0;
    for (std::size_t i = fromFrame * 2; i < s.size(); ++i, ++n) sum += static_cast<double>(s[i]) * s[i];
    return std::sqrt(sum / static_cast<double>(n));
}

// Steady-state gain in dB of `spec` for a sine at `hz`.
double responseDb(const EffectSpec& spec, double hz) {
    auto p = createProcessor(spec);
    EXPECT_TRUE(p.ok());
    p.value()->prepare(kRate, 2);
    auto buf = sine(hz, 0.1, kRate / 2);
    const double in = rms(buf, kRate / 4);
    p.value()->process(buf.data(), kRate / 2);
    return 20 * std::log10(rms(buf, kRate / 4) / in);  // skip the filter's settling time
}

EffectSpec effect(const std::string& type, std::map<std::string, double> params) {
    auto spec = makeEffect(type);
    EXPECT_TRUE(spec.ok());
    for (auto& [k, v] : params) spec.value().params[k] = v;
    return spec.value();
}

}  // namespace

TEST(AudioEffects, PanLawIsUnityAtCentreAndConstantPowerOnTheFarSide) {
    auto [l, r] = panGains(0);
    EXPECT_FLOAT_EQ(l, 1.0f);
    EXPECT_FLOAT_EQ(r, 1.0f);
    std::tie(l, r) = panGains(-1);
    EXPECT_FLOAT_EQ(l, 1.0f);
    EXPECT_NEAR(r, 0.0f, 1e-6);
    std::tie(l, r) = panGains(0.5);
    EXPECT_NEAR(l, std::sqrt(2.0) * std::cos(0.75 * kPi / 2), 1e-6);
    EXPECT_FLOAT_EQ(r, 1.0f);
}

TEST(AudioEffects, GainEffect) {
    EXPECT_NEAR(responseDb(effect("gain", {{"gain", -6}}), 1000), -6, 0.01);
}

TEST(AudioEffects, EqBandsHitTheirTargets) {
    const auto low = effect("eq3", {{"lowFreq", 120}, {"lowGain", 12}});
    EXPECT_NEAR(responseDb(low, 30), 12, 1.0);
    EXPECT_NEAR(responseDb(low, 5000), 0, 0.3);
    const auto mid = effect("eq3", {{"midFreq", 1000}, {"midGain", 6}, {"midQ", 1.0}});
    EXPECT_NEAR(responseDb(mid, 1000), 6, 0.2);
    EXPECT_NEAR(responseDb(mid, 60), 0, 0.3);
    const auto high = effect("eq3", {{"highFreq", 8000}, {"highGain", -12}});
    EXPECT_NEAR(responseDb(high, 18000), -12, 1.5);
    EXPECT_NEAR(responseDb(high, 200), 0, 0.3);
    EXPECT_NEAR(responseDb(effect("eq3", {}), 440), 0, 0.01);  // flat by default
}

TEST(AudioEffects, CompressorReducesLevelsAboveThreshold) {
    const auto comp = effect("compressor", {{"threshold", -20}, {"ratio", 4}, {"attack", 1}, {"release", 50}});
    // A 0 dBFS-peak sine is 20 dB over: the output peak settles near -20 + 20/4 = -15 dBFS.
    auto p = createProcessor(comp);
    ASSERT_TRUE(p.ok());
    p.value()->prepare(kRate, 2);
    auto loud = sine(1000, 1.0, kRate / 2);
    p.value()->process(loud.data(), kRate / 2);
    float peak = 0;
    for (std::size_t i = loud.size() / 2; i < loud.size(); ++i) peak = std::max(peak, std::abs(loud[i]));
    EXPECT_NEAR(20 * std::log10(peak), -15, 1.0);
    // Below the threshold nothing changes.
    EXPECT_NEAR(responseDb(effect("compressor", {{"threshold", -20}}), 1000), 0, 0.05);  // 0.1 amplitude = -20 dBFS peak
    // Make-up gain applies on top.
    EXPECT_NEAR(responseDb(effect("compressor", {{"threshold", -10}, {"makeup", 6}}), 1000), 6, 0.05);
}

TEST(AudioEffects, ResetClearsFilterState) {
    const auto spec = effect("eq3", {{"lowGain", 12}});
    auto a = createProcessor(spec);
    auto b = createProcessor(spec);
    a.value()->prepare(kRate, 2);
    b.value()->prepare(kRate, 2);
    auto noise = sine(50, 0.8, 2000);
    a.value()->process(noise.data(), 2000);
    a.value()->reset();
    auto x = sine(300, 0.3, 500);
    auto y = x;
    a.value()->process(x.data(), 500);
    b.value()->process(y.data(), 500);
    EXPECT_EQ(x, y);
}

TEST(AudioEffects, ValidationAndDefaults) {
    EXPECT_EQ(makeEffect("reverb").error().code, ErrorCode::NotFound);
    auto spec = makeEffect("compressor");
    ASSERT_TRUE(spec.ok());
    EXPECT_EQ(spec.value().param("ratio"), 4);
    EXPECT_FALSE(spec.value().id.empty());
    spec.value().params["ratio"] = 0.5;
    EXPECT_EQ(validateEffect(spec.value()).error().code, ErrorCode::OutOfRange);
    spec.value().params.erase("ratio");
    spec.value().params["knee"] = 1;
    EXPECT_EQ(validateEffect(spec.value()).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(effectTypes().size(), 3u);
}
