#include <gtest/gtest.h>

#include "render/Compositing.h"
#include "timeline/Animation.h"

using namespace up;
using namespace up::render;

TEST(Animation, ConstantWithoutKeys) {
    AnimatedValue v;
    v.value = 42;
    EXPECT_FALSE(v.animated());
    EXPECT_EQ(v.at(-5), 42);
    EXPECT_EQ(v.at(1000), 42);
}

TEST(Animation, InterpolatesBetweenKeysAndHoldsOutside) {
    AnimatedValue v;
    v.setKey(20, 100);
    v.setKey(10, 0);  // inserted in order
    ASSERT_EQ(v.keys.size(), 2u);
    EXPECT_EQ(v.keys[0].frame, 10);
    EXPECT_DOUBLE_EQ(v.at(0), 0);     // before the first key
    EXPECT_DOUBLE_EQ(v.at(15), 50);   // linear midpoint
    EXPECT_DOUBLE_EQ(v.at(12), 20);
    EXPECT_DOUBLE_EQ(v.at(30), 100);  // after the last key

    v.setKey(10, 0, Interpolation::Hold);
    EXPECT_DOUBLE_EQ(v.at(19), 0);
    EXPECT_DOUBLE_EQ(v.at(20), 100);

    v.setKey(10, 0, Interpolation::EaseInOut);
    EXPECT_DOUBLE_EQ(v.at(15), 50);   // symmetric
    EXPECT_LT(v.at(11), 10 * 0.5);    // slow start (linear would be 10)
    EXPECT_GT(v.at(19), 100 - 5);     // slow finish
    EXPECT_TRUE(v.removeKey(10));
    EXPECT_FALSE(v.removeKey(10));
    EXPECT_DOUBLE_EQ(v.at(0), 100);
}

TEST(Animation, ReplacingAKeyKeepsItsInterpolationUnlessGiven) {
    AnimatedValue v;
    v.setKey(5, 1, Interpolation::Hold);
    v.setKey(5, 2);
    ASSERT_EQ(v.keys.size(), 1u);
    EXPECT_EQ(v.keys[0].value, 2);
    EXPECT_EQ(v.keys[0].interpolation, Interpolation::Hold);
}

TEST(Animation, ParamMetadataAndIdentity) {
    ClipTransform t;
    EXPECT_TRUE(t.isIdentity());
    EXPECT_EQ(t[ClipParam::Scale].value, 100);
    EXPECT_EQ(t[ClipParam::Opacity].value, 100);
    EXPECT_EQ(clipParamFromString("cropLeft"), ClipParam::CropLeft);
    EXPECT_FALSE(clipParamFromString("zoom").has_value());
    EXPECT_EQ(interpolationFromString("ease"), Interpolation::EaseInOut);
    t[ClipParam::Rotation].setKey(0, 0);
    EXPECT_FALSE(t.isIdentity());
}

namespace {

VideoFrame solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    VideoFrame f(w, h);
    f.fill(r, g, b);
    return f;
}

const uint8_t* px(const VideoFrame& f, int x, int y) { return f.row(y) + x * 4; }

}  // namespace

TEST(Compositing, OneToOneCopyWithOffsetAndOpacity) {
    VideoFrame canvas = solid(40, 20, 0, 0, 255);
    const VideoFrame red = solid(10, 10, 255, 0, 0);
    LayerPlacement p;
    p.centerX = 25;  // covers x 20..29
    p.centerY = 10;  // covers y 5..14
    compositeOver(canvas, red, p);
    EXPECT_EQ(px(canvas, 20, 5)[0], 255);
    EXPECT_EQ(px(canvas, 29, 14)[0], 255);
    EXPECT_EQ(px(canvas, 19, 10)[2], 255);  // just outside stays blue
    EXPECT_EQ(px(canvas, 30, 10)[2], 255);

    p.opacity = 0.5;
    VideoFrame half = solid(40, 20, 0, 0, 255);
    compositeOver(half, red, p);
    EXPECT_NEAR(px(half, 25, 10)[0], 128, 1);
    EXPECT_NEAR(px(half, 25, 10)[2], 128, 1);
    EXPECT_EQ(px(half, 25, 10)[3], 255);
}

TEST(Compositing, ScaleCropAndClipToCanvas) {
    VideoFrame canvas = solid(40, 40, 0, 0, 0);
    const VideoFrame white = solid(10, 10, 255, 255, 255);
    LayerPlacement p;
    p.centerX = 20;
    p.centerY = 20;
    p.scale = 2.0;       // 20x20 at 10..30
    p.cropLeft = 0.5;    // left half removed: 20..30 visible
    compositeOver(canvas, white, p);
    EXPECT_EQ(px(canvas, 15, 20)[0], 0);
    EXPECT_EQ(px(canvas, 25, 20)[0], 255);
    EXPECT_EQ(px(canvas, 25, 9)[0], 0);
    EXPECT_EQ(px(canvas, 25, 29)[0], 255);
    EXPECT_EQ(px(canvas, 25, 30)[0], 0);

    // Placed partly off-canvas: no crash, visible part drawn.
    VideoFrame edge = solid(40, 40, 0, 0, 0);
    p.cropLeft = 0;
    p.centerX = 0;
    p.centerY = 0;
    compositeOver(edge, white, p);
    EXPECT_EQ(px(edge, 5, 5)[0], 255);
    EXPECT_EQ(px(edge, 15, 15)[0], 0);
}

TEST(Compositing, RotationTurnsAWideBarUpright) {
    VideoFrame canvas = solid(40, 40, 0, 0, 0);
    const VideoFrame bar = solid(20, 4, 255, 255, 255);
    LayerPlacement p;
    p.centerX = 20;
    p.centerY = 20;
    p.rotationDegrees = 90;
    compositeOver(canvas, bar, p);
    EXPECT_EQ(px(canvas, 20, 12)[0], 255);  // now spans vertically
    EXPECT_EQ(px(canvas, 20, 27)[0], 255);
    EXPECT_EQ(px(canvas, 12, 20)[0], 0);    // and no longer horizontally
    EXPECT_EQ(px(canvas, 27, 20)[0], 0);
}

TEST(Compositing, IgnoresInvisibleLayers) {
    VideoFrame canvas = solid(8, 8, 1, 2, 3);
    const VideoFrame red = solid(8, 8, 255, 0, 0);
    LayerPlacement p;
    p.centerX = 4;
    p.centerY = 4;
    p.opacity = 0;
    compositeOver(canvas, red, p);
    p.opacity = 1;
    p.scale = 0;
    compositeOver(canvas, red, p);
    p.scale = 1;
    p.cropLeft = 0.6;
    p.cropRight = 0.6;
    compositeOver(canvas, red, p);
    EXPECT_EQ(px(canvas, 4, 4)[0], 1);
}
