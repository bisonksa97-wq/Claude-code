#include "render/ColorGrading.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "render/ColorCurves.h"
#include "render/ColorManagement.h"
#include "render/Parallel.h"

namespace up::render {
namespace {

constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;

}  // namespace

bool GradeValues::isIdentity() const {
    for (std::size_t i = 0; i < kGradeParamCount; ++i)
        if (v[i] != gradeInfo(static_cast<GradeParam>(i)).defaultValue) return false;
    return true;
}

GradeValues defaultGrade() {
    GradeValues g;
    for (std::size_t i = 0; i < kGradeParamCount; ++i) g.v[i] = gradeInfo(static_cast<GradeParam>(i)).defaultValue;
    return g;
}

GradeValues evaluateGrade(const ClipGrade& grade, FrameIndex sourceFrame) {
    GradeValues g;
    for (std::size_t i = 0; i < kGradeParamCount; ++i) {
        const GradeParamInfo& info = gradeInfo(static_cast<GradeParam>(i));
        g.v[i] = std::clamp(grade.values[i].at(sourceFrame), info.minimum, info.maximum);
    }
    return g;
}

double srgbToLinear(double e) {
    return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double l) {
    if (l <= 0.0) return l * 12.92;  // keep negatives linear (they are clamped later)
    return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
}

double gradeChannel(const GradeValues& g, int channel, double encoded, Transfer transfer) {
    static constexpr GradeParam lift[3] = {GradeParam::LiftR, GradeParam::LiftG, GradeParam::LiftB};
    static constexpr GradeParam gamma[3] = {GradeParam::GammaR, GradeParam::GammaG, GradeParam::GammaB};
    static constexpr GradeParam gain[3] = {GradeParam::GainR, GradeParam::GainG, GradeParam::GainB};
    static constexpr GradeParam offset[3] = {GradeParam::OffsetR, GradeParam::OffsetG, GradeParam::OffsetB};

    double v = encoded;
    // Linear light: exposure and white balance.
    const double exposure = g[GradeParam::Exposure];
    const double temperature = g[GradeParam::Temperature] / 100.0;
    const double tint = g[GradeParam::Tint] / 100.0;
    if (exposure != 0.0 || temperature != 0.0 || tint != 0.0) {
        double lin = decodeTransfer(transfer, v) * std::exp2(exposure);
        // Warmer (+temperature) raises red and lowers blue; +tint moves towards magenta (less green).
        if (channel == 0) lin *= 1.0 + 0.3 * temperature;
        if (channel == 2) lin *= 1.0 - 0.3 * temperature;
        if (channel == 1) lin *= 1.0 - 0.3 * tint;
        v = encodeTransfer(transfer, lin);
    }
    // Encoded values: offset, lift/gain (lift raises blacks, keeps white), gamma, contrast.
    v += g[GradeParam::OffsetMaster] + g[offset[channel]];
    const double l = g[GradeParam::LiftMaster] + g[lift[channel]];
    const double k = g[GradeParam::GainMaster] * g[gain[channel]];
    v = k * (v + l * (1.0 - v));
    const double gm = g[GradeParam::GammaMaster] * g[gamma[channel]];
    if (gm != 1.0) v = v > 0.0 ? std::pow(v, 1.0 / gm) : v;  // gamma > 1 brightens the mid-tones
    const double pivot = g[GradeParam::Pivot];
    v = (v - pivot) * g[GradeParam::Contrast] + pivot;
    return v;
}

namespace {

// Per-channel tables sample the primaries and tone curves at 16 steps per 8-bit code,
// so 8-bit input (k / 255 = 16k / 4080) hits entries exactly; other values in 0..1 are
// interpolated (error far below one 8-bit step) and values outside 0..1 are computed.
constexpr int kTableSteps = 255 * 16;

}  // namespace

void applyGrade(FloatFrame& frame, const GradeValues& g, const GradeCurves& curves, const Lut* lut, Transfer transfer) {
    if (frame.empty()) return;
    auto curve = [&](CurveKind k) { return CurveEvaluator(k, curves[static_cast<std::size_t>(k)]); };
    const CurveEvaluator master = curve(CurveKind::Master);
    const std::array<CurveEvaluator, 3> channelCurves = {curve(CurveKind::Red), curve(CurveKind::Green), curve(CurveKind::Blue)};
    const CurveEvaluator hueVsHue = curve(CurveKind::HueVsHue);
    const CurveEvaluator hueVsSat = curve(CurveKind::HueVsSat);
    const CurveEvaluator lumVsSat = curve(CurveKind::LumVsSat);
    const bool toneCurves = !master.isIdentity() || !channelCurves[0].isIdentity() || !channelCurves[1].isIdentity() ||
                            !channelCurves[2].isIdentity();
    const bool hueCurves = !hueVsHue.isIdentity() || !hueVsSat.isIdentity();
    const float saturation = static_cast<float>(g[GradeParam::Saturation]);
    const bool chroma = saturation != 1.0f || hueCurves || !lumVsSat.isIdentity();
    const bool primaries = !g.isIdentity() || toneCurves;
    if (!primaries && !chroma && !lut) return;

    auto channelValue = [&](int c, double v) {
        v = gradeChannel(g, c, v, transfer);
        if (toneCurves) v = channelCurves[static_cast<std::size_t>(c)](master(v));  // curves work on 0..1
        return v;
    };
    std::array<std::vector<float>, 3> table;
    if (primaries) {
        for (int c = 0; c < 3; ++c) {
            auto& t = table[static_cast<std::size_t>(c)];
            t.resize(kTableSteps + 1);
            for (int i = 0; i <= kTableSteps; ++i) t[static_cast<std::size_t>(i)] = static_cast<float>(channelValue(c, static_cast<double>(i) / kTableSteps));
        }
    }
    auto lookup = [&](int c, float v) -> float {
        if (!(v >= 0.0f && v <= 1.0f)) return static_cast<float>(channelValue(c, v));
        const auto& t = table[static_cast<std::size_t>(c)];
        const double x = static_cast<double>(v) * kTableSteps;
        const double i = std::floor(x);
        const double f = x - i;
        const auto k = static_cast<std::size_t>(i);
        if (f < 1e-3 || k >= kTableSteps) return t[f < 1e-3 ? k : std::min<std::size_t>(k + 1, kTableSteps)];
        if (f > 1.0 - 1e-3) return t[k + 1];
        return static_cast<float>(t[k] + (t[k + 1] - t[k]) * f);
    };

    parallelRows(frame.height, static_cast<std::size_t>(frame.width) * (chroma || lut ? 16 : 4), [&](int y0, int y1) {
    for (std::size_t i = static_cast<std::size_t>(y0) * frame.width * 4; i < static_cast<std::size_t>(y1) * frame.width * 4; i += 4) {
        float r = frame.pixels[i];
        float gr = frame.pixels[i + 1];
        float b = frame.pixels[i + 2];
        if (primaries) {
            r = lookup(0, r);
            gr = lookup(1, gr);
            b = lookup(2, b);
        }
        if (chroma) {
            const auto y = static_cast<float>(kLumaR * r + kLumaG * gr + kLumaB * b);
            float cb = (b - y) / 1.8556f;
            float cr = (r - y) / 1.5748f;
            double scale = saturation;
            if (!lumVsSat.isIdentity()) scale *= 2.0 * lumVsSat(y);
            if (hueCurves && (cb != 0.0f || cr != 0.0f)) {
                const double hue = hueOf(cb, cr);
                if (!hueVsSat.isIdentity()) scale *= 2.0 * hueVsSat(hue);
                if (!hueVsHue.isIdentity()) {
                    const double turn = (hueVsHue(hue) - 0.5) * 2.0 * std::numbers::pi;
                    const double c = std::cos(turn), s = std::sin(turn);
                    const double rb = cb * c - cr * s;
                    const double rr = cb * s + cr * c;
                    cb = static_cast<float>(rb);
                    cr = static_cast<float>(rr);
                }
            }
            cb *= static_cast<float>(scale);
            cr *= static_cast<float>(scale);
            r = y + 1.5748f * cr;
            b = y + 1.8556f * cb;
            gr = static_cast<float>((y - kLumaR * r - kLumaB * b) / kLumaG);
        }
        if (lut) {
            const auto out = lut->apply(r, gr, b);
            r = out[0];
            gr = out[1];
            b = out[2];
        }
        frame.pixels[i] = r;
        frame.pixels[i + 1] = gr;
        frame.pixels[i + 2] = b;
    }
    });
}

void applyGrade(VideoFrame& frame, const GradeValues& g, const GradeCurves& curves, const Lut* lut) {
    if (frame.empty()) return;
    FloatFrame f = toFloatFrame(frame);
    applyGrade(f, g, curves, lut, Transfer::SRGB);
    frame = toVideoFrame(f);
}

void applyLut(FloatFrame& frame, const Lut& lut) {
    parallelRows(frame.height, static_cast<std::size_t>(frame.width) * 16, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* px = frame.row(y);
            for (int x = 0; x < frame.width; ++x, px += 4) {
                const auto out = lut.apply(px[0], px[1], px[2]);
                px[0] = out[0];
                px[1] = out[1];
                px[2] = out[2];
            }
        }
    });
}

void applyLut(VideoFrame& frame, const Lut& lut) {
    for (std::size_t i = 0; i + 3 < frame.pixels.size(); i += 4) {
        const auto out = lut.apply(frame.pixels[i] / 255.0f, frame.pixels[i + 1] / 255.0f, frame.pixels[i + 2] / 255.0f);
        for (int c = 0; c < 3; ++c)
            frame.pixels[i + static_cast<std::size_t>(c)] = static_cast<uint8_t>(std::lround(std::clamp(out[static_cast<std::size_t>(c)], 0.0f, 1.0f) * 255.0f));
    }
}

}  // namespace up::render
