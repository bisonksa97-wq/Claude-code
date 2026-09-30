#include <gtest/gtest.h>

#include <cmath>

#include "render/ColorCurves.h"
#include "render/ColorGrading.h"
#include "render/Lut.h"
#include "support/TestSupport.h"

using namespace up;
using namespace up::render;

namespace {

std::array<uint8_t, 3> gradePixel(uint8_t r, uint8_t g, uint8_t b, const GradeCurves& curves, const Lut* lut = nullptr,
                                  GradeValues values = defaultGrade()) {
    VideoFrame f(1, 1);
    f.fill(r, g, b);
    applyGrade(f, values, curves, lut);
    return {f.pixels[0], f.pixels[1], f.pixels[2]};
}

GradeCurves withCurve(CurveKind kind, std::vector<CurvePoint> points) {
    GradeCurves c{};
    c[static_cast<std::size_t>(kind)] = std::move(points);
    return c;
}

Lut parsed(const std::string& text) {
    auto lut = parseCubeLut(text);
    EXPECT_TRUE(lut.ok()) << (lut.ok() ? "" : lut.error().toString());
    return lut.ok() ? lut.value() : Lut{};
}

std::array<float, 3> identity(float r, float g, float b) { return {r, g, b}; }

}  // namespace

TEST(Curves, ToneCurvesPassThroughPointsMonotonically) {
    EXPECT_TRUE(CurveEvaluator(CurveKind::Master, {}).isIdentity());
    EXPECT_DOUBLE_EQ(CurveEvaluator(CurveKind::Master, {})(0.3), 0.3);
    // A straight line is reproduced exactly.
    const CurveEvaluator line(CurveKind::Red, {{0, 0}, {1, 1}});
    for (double x = 0; x <= 1.0; x += 0.05) EXPECT_NEAR(line(x), x, 1e-12);

    const std::vector<CurvePoint> points = {{0, 0}, {0.25, 0.4}, {0.75, 0.6}, {1, 1}};
    const CurveEvaluator s(CurveKind::Master, points);
    for (const auto& p : points) EXPECT_NEAR(s(p.x), p.y, 1e-12);
    double previous = -1.0;
    for (int i = 0; i <= 1000; ++i) {
        const double y = s(i / 1000.0);
        EXPECT_GE(y, previous - 1e-12);
        previous = y;
    }
    // Flat outside the first and last point.
    const CurveEvaluator inner(CurveKind::Green, {{0.2, 0.1}, {0.8, 0.9}});
    EXPECT_DOUBLE_EQ(inner(0.05), 0.1);
    EXPECT_DOUBLE_EQ(inner(0.95), 0.9);
}

TEST(Curves, SharpStepsDoNotOvershoot) {
    const CurveEvaluator step(CurveKind::Master, {{0, 0}, {0.5, 0}, {0.6, 1}, {1, 1}});
    double previous = 0.0;
    for (int i = 0; i <= 1000; ++i) {
        const double x = i / 1000.0;
        const double y = step(x);
        EXPECT_GE(y, 0.0);
        EXPECT_LE(y, 1.0);
        EXPECT_GE(y, previous - 1e-12);
        if (x <= 0.5) {
            EXPECT_DOUBLE_EQ(y, 0.0);
        }
        if (x >= 0.6) {
            EXPECT_DOUBLE_EQ(y, 1.0);
        }
        previous = y;
    }
}

TEST(Curves, HueCurvesWrapAndValidate) {
    const CurveEvaluator empty(CurveKind::HueVsSat, {});
    EXPECT_DOUBLE_EQ(empty(0.3), 0.5);  // neutral
    const CurveEvaluator one(CurveKind::HueVsHue, {{0.4, 0.7}});
    EXPECT_DOUBLE_EQ(one(0.0), 0.7);
    EXPECT_DOUBLE_EQ(one(0.9), 0.7);
    const CurveEvaluator two(CurveKind::HueVsSat, {{0.1, 0.8}, {0.6, 0.2}});
    EXPECT_NEAR(two(0.1), 0.8, 1e-12);
    EXPECT_NEAR(two(0.6), 0.2, 1e-12);
    EXPECT_NEAR(two(0.0), two(1.0 - 1e-9), 1e-6);  // continuous across hue 0/1
    EXPECT_NEAR(two(1.1), two(0.1), 1e-12);

    std::vector<CurvePoint> unsorted = {{0.9, 0.5}, {0.1, 0.2}};
    EXPECT_FALSE(validateCurve(CurveKind::Master, unsorted));
    EXPECT_DOUBLE_EQ(unsorted[0].x, 0.1);
    std::vector<CurvePoint> single = {{0.5, 0.5}};
    EXPECT_TRUE(validateCurve(CurveKind::Master, single));
    EXPECT_FALSE(validateCurve(CurveKind::HueVsHue, single));
    std::vector<CurvePoint> outside = {{0.0, 0.0}, {1.2, 1.0}};
    EXPECT_TRUE(validateCurve(CurveKind::Blue, outside));
    std::vector<CurvePoint> same = {{0.5, 0.0}, {0.5, 1.0}};
    EXPECT_TRUE(validateCurve(CurveKind::Red, same));
    std::vector<CurvePoint> bothEnds = {{0.0, 0.5}, {1.0, 0.5}};
    EXPECT_TRUE(validateCurve(CurveKind::HueVsSat, bothEnds));
    std::vector<CurvePoint> tooMany(kMaxCurvePoints + 1);
    for (std::size_t i = 0; i < tooMany.size(); ++i) tooMany[i] = {static_cast<double>(i) / 100.0, 0.5};
    EXPECT_TRUE(validateCurve(CurveKind::LumVsSat, tooMany));
}

TEST(Curves, HueRunsRedYellowGreenCyanBlueMagenta) {
    auto hue = [](double r, double g, double b) {
        const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        return hueOf((b - y) / 1.8556, (r - y) / 1.5748);
    };
    EXPECT_NEAR(hue(1, 0, 0), 0.0, 1e-9);
    const double yellow = hue(1, 1, 0), green = hue(0, 1, 0), cyan = hue(0, 1, 1), blue = hue(0, 0, 1), magenta = hue(1, 0, 1);
    EXPECT_LT(0.0, yellow);
    EXPECT_LT(yellow, green);
    EXPECT_LT(green, cyan);
    EXPECT_LT(cyan, blue);
    EXPECT_LT(blue, magenta);
    EXPECT_LT(magenta, 1.0);
}

TEST(Grading, CurvesInTheGradingPass) {
    // Master curve inverting the picture.
    auto inverted = gradePixel(200, 100, 30, withCurve(CurveKind::Master, {{0, 1}, {1, 0}}));
    EXPECT_NEAR(inverted[0], 55, 1);
    EXPECT_NEAR(inverted[1], 155, 1);
    EXPECT_NEAR(inverted[2], 225, 1);
    // Red curve halving red only.
    auto halved = gradePixel(200, 100, 30, withCurve(CurveKind::Red, {{0, 0}, {1, 0.5}}));
    EXPECT_NEAR(halved[0], 100, 1);
    EXPECT_EQ(halved[1], 100);
    EXPECT_EQ(halved[2], 30);
    // Hue-vs-sat and lum-vs-sat at zero desaturate while keeping Rec.709 luma.
    for (CurveKind kind : {CurveKind::HueVsSat, CurveKind::LumVsSat}) {
        auto grey = gradePixel(180, 100, 100, withCurve(kind, {{0.2, 0.0}, {0.7, 0.0}}));
        EXPECT_NEAR(grey[0], 117, 1);
        EXPECT_NEAR(grey[1], 117, 1);
        EXPECT_NEAR(grey[2], 117, 1);
    }
    // Hue-vs-hue at 1.0 rotates the hue half a turn: each channel mirrors around luma.
    auto rotated = gradePixel(180, 100, 100, withCurve(CurveKind::HueVsHue, {{0.5, 1.0}}));
    EXPECT_NEAR(rotated[0], 54, 1);
    EXPECT_NEAR(rotated[1], 134, 1);
    EXPECT_NEAR(rotated[2], 134, 1);
    // Hue-vs-sat only touching blues leaves a red alone.
    auto untouched = gradePixel(180, 100, 100, withCurve(CurveKind::HueVsSat, {{0.0, 0.5}, {0.45, 0.5}, {0.7, 0.0}, {0.9, 0.5}}));
    EXPECT_EQ(untouched, (std::array<uint8_t, 3>{180, 100, 100}));
}

TEST(Lut, IdentityAndLinearTablesAreExact) {
    const Lut id = parsed(test::cubeText(2, identity));
    EXPECT_TRUE(id.is3D);
    EXPECT_EQ(id.size, 2);
    EXPECT_EQ(id.title, "test");
    const Lut swap = parsed(test::cubeText(17, [](float r, float g, float b) { return std::array<float, 3>{g, b, r}; }));
    for (float r : {0.0f, 0.13f, 0.5f, 0.77f, 1.0f})
        for (float g : {0.0f, 0.31f, 0.9f})
            for (float b : {0.05f, 0.6f, 1.0f}) {
                const auto out = id.apply(r, g, b);
                EXPECT_NEAR(out[0], r, 1e-5);
                EXPECT_NEAR(out[1], g, 1e-5);
                EXPECT_NEAR(out[2], b, 1e-5);
                const auto s = swap.apply(r, g, b);
                EXPECT_NEAR(s[0], g, 1e-5);
                EXPECT_NEAR(s[1], b, 1e-5);
                EXPECT_NEAR(s[2], r, 1e-5);
            }
    // Input outside the domain is clamped.
    EXPECT_NEAR(id.apply(1.5f, -0.2f, 0.5f)[0], 1.0f, 1e-6);
    EXPECT_NEAR(id.apply(1.5f, -0.2f, 0.5f)[1], 0.0f, 1e-6);
}

TEST(Lut, NonLinearTablesHitLatticePointsAndInterpolateClosely) {
    auto square = [](float r, float g, float b) { return std::array<float, 3>{r * r, g * g, b * b}; };
    const Lut lut = parsed(test::cubeText(33, square));
    for (int i = 0; i < 33; i += 4) {
        const float v = static_cast<float>(i) / 32.0f;
        EXPECT_NEAR(lut.apply(v, v, v)[0], v * v, 1e-5);
    }
    for (float v : {0.1f, 0.33f, 0.71f}) EXPECT_NEAR(lut.apply(v, 0.2f, 0.9f)[0], v * v, 1e-3);
    // A 1D LUT applies per channel.
    const Lut inverse = parsed(test::cubeText(5, [](float r, float g, float b) { return std::array<float, 3>{1 - r, 1 - g, 1 - b}; }, false));
    EXPECT_FALSE(inverse.is3D);
    const auto out = inverse.apply(0.1f, 0.5f, 0.8f);
    EXPECT_NEAR(out[0], 0.9f, 1e-5);
    EXPECT_NEAR(out[1], 0.5f, 1e-5);
    EXPECT_NEAR(out[2], 0.2f, 1e-5);
    // Input range keyword and domain.
    const Lut ranged = parsed("LUT_3D_INPUT_RANGE 0 2\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    EXPECT_NEAR(ranged.apply(1.0f, 1.0f, 1.0f)[0], 0.5f, 1e-6);
}

TEST(Lut, MalformedFilesAreRejectedWithTheLine) {
    const std::string lattice = "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    struct Case {
        std::string text;
        std::string expect;
    };
    const Case cases[] = {
        {"", "no table data"},
        {"0 0 0\n", "before LUT_3D_SIZE"},
        {"LUT_3D_SIZE 1\n", "between 2 and"},
        {"LUT_3D_SIZE 500\n", "between 2 and"},
        {"LUT_3D_SIZE 2\n" + lattice.substr(0, 18), "table entries but the size needs 8"},
        {"LUT_3D_SIZE 2\n" + lattice + "1 1 1\n", "more table entries"},
        {"LUT_3D_SIZE 2\n0 0 zero\n", "three numbers"},
        {"LUT_3D_SIZE 2\n0 0\n", "three numbers"},
        {"LUT_3D_SIZE 2\nLUT_1D_SIZE 4\n" + lattice, "both"},
        {"DOMAIN_MIN 1 1 1\nDOMAIN_MAX 0 0 0\nLUT_3D_SIZE 2\n" + lattice, "DOMAIN_MAX"},
        {"LUT_3D_SIZE 2\n" + lattice.substr(0, 12) + "TITLE \"late\"\n", "keyword after"},
    };
    for (const auto& c : cases) {
        auto lut = parseCubeLut(c.text, "'bad.cube'");
        ASSERT_FALSE(lut.ok()) << c.text;
        EXPECT_NE(lut.error().message.find(c.expect), std::string::npos) << lut.error().message;
        EXPECT_NE(lut.error().message.find("bad.cube"), std::string::npos);
    }
    EXPECT_EQ(parseCubeLut("LUT_3D_SIZE 2\n0 0 zero\n").error().details, "line 2");
    // Vendor keywords and comments are fine.
    EXPECT_TRUE(parseCubeLut("# c\nLUT_IN_VIDEO_RANGE\nLUT_3D_SIZE 2\n" + lattice + "\n# end\n").ok());
}

TEST(Lut, CacheReloadsChangedFilesAndReportsMissingOnes) {
    test::TempDir dir;
    const auto path = dir / "look.cube";
    test::writeText(path, test::cubeText(2, identity));
    LutCache cache;
    auto first = cache.get(path);
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(cache.get(path).value(), first.value());  // cached
    test::writeText(path, test::cubeText(3, identity));
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
    auto second = cache.get(path);
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(second.value()->size, 3);
    std::filesystem::remove(path);
    auto missing = cache.get(path);
    ASSERT_FALSE(missing.ok());
    EXPECT_NE(missing.error().suggestion.find("Relink"), std::string::npos);
    EXPECT_FALSE(loadCubeLut(dir / "look.txt").ok());
}

TEST(Grading, LutIsAppliedLastWithOneQuantisation) {
    const Lut swap = parsed(test::cubeText(9, [](float r, float g, float b) { return std::array<float, 3>{g, b, r}; }));
    EXPECT_EQ(gradePixel(200, 50, 10, {}, &swap), (std::array<uint8_t, 3>{50, 10, 200}));
    // Desaturate first, then the LUT sees grey.
    GradeValues grey = defaultGrade();
    grey[GradeParam::Saturation] = 0.0;
    const auto out = gradePixel(200, 50, 10, {}, &swap, grey);
    EXPECT_NEAR(out[0], out[1], 1);
    EXPECT_NEAR(out[1], out[2], 1);
    // Output LUT on a whole frame.
    VideoFrame f(2, 1);
    f.pixels = {200, 50, 10, 255, 0, 0, 255, 128};
    applyLut(f, swap);
    EXPECT_EQ(f.pixels, (std::vector<uint8_t>{50, 10, 200, 255, 0, 255, 0, 128}));
}
