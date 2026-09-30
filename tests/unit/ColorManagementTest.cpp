#include <gtest/gtest.h>

#include <cmath>
#include <set>

#include "render/ColorGrading.h"
#include "render/ColorManagement.h"
#include "render/Compositing.h"
#include "render/Lut.h"
#include "render/ViewerOverlay.h"
#include "support/TestSupport.h"

using namespace up;
using namespace up::render;

namespace {

constexpr std::array<Transfer, 8> kTransfers = {Transfer::Linear, Transfer::SRGB, Transfer::BT1886, Transfer::Gamma22,
                                                Transfer::PQ,     Transfer::HLG,  Transfer::LogC3,  Transfer::SLog3};

}  // namespace

TEST(ColorManagement, TransferFunctionsRoundTrip) {
    for (Transfer t : kTransfers) {
        // Up to each encoding's peak: PQ reaches 10000 nits (~49x reference white), logs ~50x.
        const double peak = t == Transfer::PQ ? 49.0 : t == Transfer::HLG ? 3.7 : isLog(t) ? 40.0 : 1.0;
        for (double l = 0.0; l <= peak; l += peak / 97.0) {
            EXPECT_NEAR(decodeTransfer(t, encodeTransfer(t, l)), l, 1e-9 + l * 1e-9) << transferId(t) << " at " << l;
        }
        if (!isHdr(t) && !isLog(t)) {  // power laws are mirrored below zero
            EXPECT_NEAR(decodeTransfer(t, encodeTransfer(t, -0.2)), -0.2, 1e-12) << transferId(t);
        }
    }
}

TEST(ColorManagement, TransferReferenceValues) {
    EXPECT_NEAR(decodeTransfer(Transfer::SRGB, 0.5), 0.214041, 1e-6);
    EXPECT_NEAR(decodeTransfer(Transfer::BT1886, 0.5), std::pow(0.5, 2.4), 1e-12);
    EXPECT_NEAR(decodeTransfer(Transfer::Gamma22, 0.5), std::pow(0.5, 2.2), 1e-12);
    // PQ: reference white (203 cd/m²) sits at 58 % of the signal (BT.2408); 1.0 is 10000 cd/m².
    EXPECT_NEAR(encodeTransfer(Transfer::PQ, 1.0), 0.5807, 5e-4);
    EXPECT_NEAR(decodeTransfer(Transfer::PQ, 1.0), 10000.0 / 203.0, 1e-6);
    EXPECT_NEAR(decodeTransfer(Transfer::PQ, 0.0), 0.0, 1e-12);
    // HLG: 75 % signal = reference white = linear 1.0; 50 % is the knee (scene light 1/12).
    EXPECT_NEAR(encodeTransfer(Transfer::HLG, 1.0), 0.75, 1e-9);
    EXPECT_NEAR(decodeTransfer(Transfer::HLG, 0.5) * 0.264964, 1.0 / 12.0, 1e-6);
    // Camera logs: 18 % grey and black.
    EXPECT_NEAR(encodeTransfer(Transfer::LogC3, 0.18), 0.3910, 5e-4);
    EXPECT_NEAR(encodeTransfer(Transfer::LogC3, 0.0), 0.092809, 1e-6);
    EXPECT_NEAR(encodeTransfer(Transfer::SLog3, 0.18), 420.0 / 1023.0, 1e-9);
    EXPECT_NEAR(encodeTransfer(Transfer::SLog3, 0.0), 95.0 / 1023.0, 1e-9);
}

TEST(ColorManagement, PrimariesMatricesMatchPublishedValues) {
    const Matrix3 xyz = rgbToXyz(Primaries::Rec709);
    EXPECT_NEAR(xyz[1][0], 0.2126, 1e-4);  // Rec.709 luma weights are the Y row
    EXPECT_NEAR(xyz[1][1], 0.7152, 1e-4);
    EXPECT_NEAR(xyz[1][2], 0.0722, 1e-4);
    EXPECT_NEAR(xyz[0][0] + xyz[0][1] + xyz[0][2], 0.9505, 1e-4);  // white = D65
    EXPECT_NEAR(xyz[2][0] + xyz[2][1] + xyz[2][2], 1.0891, 1e-4);
    // ITU-R BT.2087: Rec.709 -> Rec.2020 in linear light.
    const Matrix3 expected = {{{0.6274, 0.3293, 0.0433}, {0.0691, 0.9195, 0.0114}, {0.0164, 0.0880, 0.8956}}};
    const Matrix3 m = primariesConversion(Primaries::Rec709, Primaries::Rec2020);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) EXPECT_NEAR(m[i][j], expected[i][j], 1e-4);
    // Every conversion keeps white white, and there and back is the identity.
    for (Primaries a : {Primaries::Rec709, Primaries::Rec2020, Primaries::P3D65, Primaries::ArriWideGamut3, Primaries::SGamut3Cine}) {
        for (Primaries b : {Primaries::Rec709, Primaries::Rec2020, Primaries::P3D65, Primaries::ArriWideGamut3}) {
            const Matrix3 ab = primariesConversion(a, b);
            const Matrix3 ba = primariesConversion(b, a);
            for (int i = 0; i < 3; ++i) {
                EXPECT_NEAR(ab[i][0] + ab[i][1] + ab[i][2], 1.0, 1e-9);
                for (int j = 0; j < 3; ++j) {
                    double v = 0;
                    for (int k = 0; k < 3; ++k) v += ab[i][k] * ba[k][j];
                    EXPECT_NEAR(v, i == j ? 1.0 : 0.0, 1e-9);
                }
            }
        }
    }
}

TEST(ColorManagement, ConversionsRoundTripAndAgreeOn8BitInput) {
    const ColorSpace video{Primaries::Rec709, Transfer::BT1886};
    const ColorSpace wide{Primaries::Rec2020, Transfer::Linear};
    EXPECT_TRUE(ColorConversion(video, video).isIdentity());
    const ColorConversion there(video, wide), back(wide, video);
    for (float r : {0.0f, 0.2f, 0.5f, 1.0f})
        for (float g : {0.1f, 0.7f})
            for (float b : {0.0f, 0.9f}) {
                const auto w = there.apply(r, g, b);
                const auto v = back.apply(w[0], w[1], w[2]);
                // A pure power law has infinite slope at 0, so float rounding of tiny linear
                // values grows to ~5e-4 in code values there: still < 1/8 of an 8-bit step.
                EXPECT_NEAR(v[0], r, 5e-4);
                EXPECT_NEAR(v[1], g, 5e-4);
                EXPECT_NEAR(v[2], b, 5e-4);
            }
    // The 8-bit path (decode table) equals the float path.
    VideoFrame f(2, 1);
    f.pixels = {200, 100, 50, 255, 10, 250, 128, 77};
    FloatFrame viaTable = there.convert(f);
    FloatFrame viaFloat = toFloatFrame(f);
    there.apply(viaFloat);
    for (std::size_t i = 0; i < viaTable.pixels.size(); ++i) EXPECT_NEAR(viaTable.pixels[i], viaFloat.pixels[i], 1e-6);
    EXPECT_FLOAT_EQ(viaTable.pixels[7], 77 / 255.0f);  // alpha untouched
    // sRGB grey 0.5 shown on a gamma 2.4 display keeps its light: 0.214^(1/2.4).
    const auto grey = ColorConversion({Primaries::Rec709, Transfer::SRGB}, video).apply(0.5f, 0.5f, 0.5f);
    EXPECT_NEAR(grey[0], std::pow(0.214041, 1.0 / 2.4), 1e-5);
}

TEST(ColorManagement, SpacesDetectFromTagsAndHaveStableIds) {
    EXPECT_EQ(detectColorSpace("", "", false), (ColorSpace{Primaries::Rec709, Transfer::BT1886}));
    EXPECT_EQ(detectColorSpace("", "", true), (ColorSpace{Primaries::Rec709, Transfer::SRGB}));
    EXPECT_EQ(detectColorSpace("bt709", "bt709", false), (ColorSpace{Primaries::Rec709, Transfer::BT1886}));
    EXPECT_EQ(detectColorSpace("bt2020", "smpte2084", false), (ColorSpace{Primaries::Rec2020, Transfer::PQ}));
    EXPECT_EQ(detectColorSpace("bt2020", "arib-std-b67", false), (ColorSpace{Primaries::Rec2020, Transfer::HLG}));
    EXPECT_EQ(detectColorSpace("smpte432", "iec61966-2-1", false), (ColorSpace{Primaries::P3D65, Transfer::SRGB}));
    for (const auto& preset : colorSpacePresets()) {
        EXPECT_EQ(ColorSpace::fromId(preset.space.id()), preset.space);
        EXPECT_EQ(preset.space.displayName(), preset.name);
        // Tags written for a space detect back to it (camera log spaces have no tags).
        const ColorTags tags = colorTagsFor(preset.space);
        if (!tags.primaries.empty() && !tags.transfer.empty()) {
            EXPECT_EQ(detectColorSpace(tags.primaries, tags.transfer, false), preset.space);
        }
    }
    EXPECT_EQ(ColorSpace::fromId("pq"), (ColorSpace{Primaries::Rec2020, Transfer::PQ}));
    EXPECT_FALSE(ColorSpace::fromId("rec709/nonsense"));
    EXPECT_EQ((ColorSpace{Primaries::Rec2020, Transfer::SRGB}).displayName(), "Rec.2020 · sRGB");
}

TEST(ColorManagement, FloatCompositingMatches8BitAndKeepsPrecision) {
    VideoFrame canvas8(8, 8), layer8(4, 4);
    canvas8.fill(10, 20, 30);
    layer8.fill(200, 150, 100);
    FloatFrame canvasF = toFloatFrame(canvas8), layerF = toFloatFrame(layer8);
    LayerPlacement p;
    p.centerX = 4.3;
    p.centerY = 3.9;
    p.scale = 1.37;
    p.rotationDegrees = 21;
    p.opacity = 0.6;
    compositeOver(canvas8, layer8, p);
    compositeOver(canvasF, layerF, p);
    const VideoFrame quantised = toVideoFrame(canvasF);
    for (std::size_t i = 0; i < quantised.pixels.size(); ++i) EXPECT_NEAR(quantised.pixels[i], canvas8.pixels[i], 1);
    // Values beyond 0..1 survive float compositing (they are only clamped at the end).
    FloatFrame bright(2, 2);
    bright.fill(3.0f, -0.5f, 0.5f);
    FloatFrame base(2, 2);
    base.fill(0, 0, 0);
    compositeOver(base, bright, LayerPlacement{1.0, 1.0, 1.0, 0.0, 1.0});
    EXPECT_FLOAT_EQ(base.pixels[0], 3.0f);
    EXPECT_FLOAT_EQ(base.pixels[1], -0.5f);
}

TEST(ColorManagement, FloatGradingAvoidsIntermediateBanding) {
    // Gain 0.25 then a x4 output LUT: quantising in between keeps only a quarter of the levels.
    const Lut times4 = parseCubeLut(test::cubeText(1024, [](float r, float g, float b) {
                                        return std::array<float, 3>{std::min(1.0f, 4 * r), std::min(1.0f, 4 * g), std::min(1.0f, 4 * b)};
                                    }, false)).value();
    GradeValues quarter = defaultGrade();
    quarter[GradeParam::GainMaster] = 0.25;
    std::set<int> floatLevels, eightBitLevels;
    for (int v = 0; v < 256; v += 1) {
        VideoFrame px(1, 1);
        px.fill(static_cast<uint8_t>(v), static_cast<uint8_t>(v), static_cast<uint8_t>(v));
        FloatFrame f = toFloatFrame(px);
        applyGrade(f, quarter, {}, nullptr, Transfer::BT1886);
        VideoFrame intermediate = toVideoFrame(f);  // what an 8-bit pipeline would pass on
        applyLut(f, times4);
        floatLevels.insert(toVideoFrame(f).pixels[0]);
        applyLut(intermediate, times4);
        eightBitLevels.insert(intermediate.pixels[0]);
    }
    EXPECT_EQ(floatLevels.size(), 256u);
    EXPECT_LE(eightBitLevels.size(), 70u);
}

TEST(ColorManagement, ViewerOverlaysMarkExposure) {
    VideoFrame f(6, 1);
    f.pixels = {255, 10, 10, 255, 0, 0, 1, 255, 102, 102, 102, 255, 140, 140, 140, 255, 5, 5, 5, 255, 251, 251, 251, 255};
    VideoFrame clip = f;
    applyOverlay(clip, ViewerOverlay::Clipping);
    EXPECT_EQ(clip.pixels[0], 255);  // clipped red channel -> red
    EXPECT_EQ(clip.pixels[1], 0);
    EXPECT_EQ(clip.pixels[6], 255);  // crushed -> blue
    EXPECT_EQ(clip.pixels[8], 102);  // mid-tones untouched
    VideoFrame fc = f;
    applyOverlay(fc, ViewerOverlay::FalseColor);
    EXPECT_EQ((std::array<uint8_t, 3>{fc.pixels[8], fc.pixels[9], fc.pixels[10]}), (std::array<uint8_t, 3>{0, 200, 0}));      // 40 %: grey card
    EXPECT_EQ((std::array<uint8_t, 3>{fc.pixels[12], fc.pixels[13], fc.pixels[14]}), (std::array<uint8_t, 3>{255, 140, 180}));  // 55 %: skin
    EXPECT_EQ((std::array<uint8_t, 3>{fc.pixels[4], fc.pixels[5], fc.pixels[6]}), (std::array<uint8_t, 3>{128, 0, 160}));     // black
    EXPECT_EQ((std::array<uint8_t, 3>{fc.pixels[20], fc.pixels[21], fc.pixels[22]}), (std::array<uint8_t, 3>{255, 230, 0}));  // 98 %
    EXPECT_EQ((std::array<uint8_t, 3>{fc.pixels[16], fc.pixels[17], fc.pixels[18]}), (std::array<uint8_t, 3>{128, 0, 160}));  // 2 %
    VideoFrame none = f;
    applyOverlay(none, ViewerOverlay::None);
    EXPECT_EQ(none.pixels, f.pixels);
}
