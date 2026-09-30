#include <gtest/gtest.h>

#include <cmath>

#include "render/ColorGrading.h"
#include "render/Scopes.h"

using namespace up;
using namespace up::render;

namespace {

GradeValues with(std::initializer_list<std::pair<GradeParam, double>> changes) {
    GradeValues g = defaultGrade();
    for (auto [p, v] : changes) g[p] = v;
    return g;
}

VideoFrame solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    VideoFrame f(w, h);
    f.fill(r, g, b);
    return f;
}

}  // namespace

TEST(Grading, DefaultsAreAnExactIdentity) {
    const GradeValues g = defaultGrade();
    EXPECT_TRUE(g.isIdentity());
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i) EXPECT_NEAR(gradeChannel(g, c, i / 255.0), i / 255.0, 1e-12);
    for (double v : {0.0, 0.001, 0.04, 0.2, 0.5, 0.9, 1.0}) EXPECT_NEAR(linearToSrgb(srgbToLinear(v)), v, 1e-12);
    // Even a grade that is identity only after quantisation leaves pixels untouched.
    VideoFrame f(3, 1);
    f.pixels = {0, 128, 255, 255, 10, 20, 30, 255, 250, 5, 99, 255};
    const auto before = f.pixels;
    applyGrade(f, with({{GradeParam::Pivot, 0.2}}));  // pivot alone changes nothing at contrast 1
    EXPECT_EQ(f.pixels, before);
}

TEST(Grading, LiftGammaGainOffsetContrast) {
    EXPECT_NEAR(gradeChannel(with({{GradeParam::GainMaster, 2}}), 0, 0.25), 0.5, 1e-12);
    const auto lift = with({{GradeParam::LiftMaster, 0.2}});
    EXPECT_NEAR(gradeChannel(lift, 1, 0.0), 0.2, 1e-12);  // blacks rise
    EXPECT_NEAR(gradeChannel(lift, 1, 1.0), 1.0, 1e-12);  // white stays
    EXPECT_NEAR(gradeChannel(with({{GradeParam::GammaMaster, 2}}), 2, 0.25), 0.5, 1e-12);
    EXPECT_NEAR(gradeChannel(with({{GradeParam::OffsetMaster, -0.1}}), 0, 0.5), 0.4, 1e-12);
    const auto contrast = with({{GradeParam::Contrast, 2}, {GradeParam::Pivot, 0.5}});
    EXPECT_NEAR(gradeChannel(contrast, 0, 0.75), 1.0, 1e-12);
    EXPECT_NEAR(gradeChannel(contrast, 0, 0.25), 0.0, 1e-12);
    // Per-channel values combine with the master: gains multiply, lifts and offsets add.
    const auto channel = with({{GradeParam::GainMaster, 2}, {GradeParam::GainR, 0.5}, {GradeParam::OffsetB, 0.1}});
    EXPECT_NEAR(gradeChannel(channel, 0, 0.3), 0.3, 1e-12);
    EXPECT_NEAR(gradeChannel(channel, 1, 0.3), 0.6, 1e-12);
    EXPECT_NEAR(gradeChannel(channel, 2, 0.3), 0.8, 1e-12);
}

TEST(Grading, ExposureAndWhiteBalanceWorkInLinearLight) {
    const double grey = linearToSrgb(0.18);  // 18% grey, encoded
    EXPECT_NEAR(gradeChannel(with({{GradeParam::Exposure, 1}}), 1, grey), linearToSrgb(0.36), 1e-12);
    const auto warm = with({{GradeParam::Temperature, 50}});
    EXPECT_GT(gradeChannel(warm, 0, grey), grey);
    EXPECT_NEAR(gradeChannel(warm, 1, grey), grey, 1e-12);
    EXPECT_LT(gradeChannel(warm, 2, grey), grey);
    EXPECT_LT(gradeChannel(with({{GradeParam::Tint, 50}}), 1, grey), grey);  // towards magenta
}

TEST(Grading, SaturationPreservesRec709LumaAndClamps) {
    VideoFrame f = solid(2, 2, 200, 50, 50);
    applyGrade(f, with({{GradeParam::Saturation, 0}}));
    const double y = (0.2126 * 200 + 0.7152 * 50 + 0.0722 * 50);
    EXPECT_NEAR(f.pixels[0], y, 1);
    EXPECT_EQ(f.pixels[0], f.pixels[1]);
    EXPECT_EQ(f.pixels[1], f.pixels[2]);
    EXPECT_EQ(f.pixels[3], 255);  // alpha untouched

    VideoFrame bright = solid(1, 1, 200, 200, 200);
    applyGrade(bright, with({{GradeParam::GainMaster, 4}}));
    EXPECT_EQ(bright.pixels[0], 255);
}

TEST(Scopes, SolidColourLandsInTheExpectedBins) {
    const VideoFrame red = solid(40, 10, 255, 0, 0);
    const Scopes s = computeScopes(red, 20);
    EXPECT_EQ(s.histogramRgb[0][255], 400u);
    EXPECT_EQ(s.histogramRgb[1][0], 400u);
    const int luma = static_cast<int>(std::lround(0.2126 * 255));
    EXPECT_EQ(s.histogramLuma[static_cast<std::size_t>(luma)], 400u);
    for (int col = 0; col < 20; ++col) {
        EXPECT_EQ(s.waveformLuma[static_cast<std::size_t>(luma) * 20 + col], 20u);  // 2 px wide x 10 rows
        EXPECT_EQ(s.parade[0][255 * 20 + col], 20u);
    }
    const auto [cb, cr] = vectorscopePosition(255, 0, 0);
    EXPECT_EQ(s.vectorscope[static_cast<std::size_t>(cr) * 256 + cb], 400u);
    EXPECT_GT(cr, 200);  // red is high Cr
    EXPECT_LT(cb, 128);
    EXPECT_EQ(s.maximum[0], 255);
    EXPECT_EQ(s.minimum[1], 0);
    EXPECT_NEAR(s.meanLuma, 0.2126, 1e-9);
    const auto grey = vectorscopePosition(128, 128, 128);
    EXPECT_EQ(grey[0], 128);  // neutral colours sit at the centre
    EXPECT_EQ(grey[1], 128);
}

TEST(Scopes, WaveformFollowsAHorizontalRamp) {
    VideoFrame ramp(256, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 256; ++x) {
            uint8_t* p = ramp.row(y) + x * 4;
            p[0] = p[1] = p[2] = static_cast<uint8_t>(x);
            p[3] = 255;
        }
    const Scopes s = computeScopes(ramp, 256);
    for (int x : {0, 64, 200, 255}) EXPECT_EQ(s.waveformLuma[static_cast<std::size_t>(x) * 256 + x], 4u);
    EXPECT_EQ(s.histogramLuma[100], 4u);
}
