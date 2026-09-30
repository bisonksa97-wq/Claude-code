#include <gtest/gtest.h>

#include <set>

#include "render/ExportPresets.h"
#include "support/TestSupport.h"

using namespace up;
using namespace up::render;

TEST(Presets, BuiltInsAreValidAndDistinct) {
    std::set<std::string> ids;
    for (const auto& p : builtInPresets()) {
        EXPECT_TRUE(validatePreset(p).ok()) << p.id << ": " << validatePreset(p).error().message;
        EXPECT_TRUE(ids.insert(p.id).second) << p.id;
        EXPECT_TRUE(p.builtIn);
    }
    const auto& all = builtInPresets();
    EXPECT_EQ(findPreset(all, "h264-web")->bitDepth(), 8);
    EXPECT_EQ(findPreset(all, "hevc-10bit")->bitDepth(), 10);
    EXPECT_EQ(findPreset(all, "prores-422hq")->bitDepth(), 10);
    EXPECT_EQ(findPreset(all, "png-16")->bitDepth(), 16);
    EXPECT_TRUE(findPreset(all, "png-16")->imageSequence());
    EXPECT_FALSE(findPreset(all, "wav-24")->video);
    EXPECT_EQ(findPreset(all, "nope"), nullptr);
    const EncodeSettings s = encodeSettingsFor(*findPreset(all, "prores-422hq"));
    EXPECT_EQ(s.videoCodec, "prores_ks");
    EXPECT_EQ(s.pixelFormat, "yuv422p10le");
    EXPECT_EQ(s.codecOptions.at("profile"), "3");
    EXPECT_EQ(s.audioCodec, "pcm_s24le");
}

TEST(Presets, JsonRoundTripAndValidation) {
    ExportPreset p = *findPreset(builtInPresets(), "hevc-10bit");
    p.id = "my-hevc";
    p.name = "My HEVC";
    p.crf = 22;
    auto back = presetFromJson(presetToJson(p));
    ASSERT_TRUE(back.ok()) << back.error().toString();
    EXPECT_EQ(back.value().id, "my-hevc");
    EXPECT_EQ(back.value().crf, 22);
    EXPECT_EQ(back.value().codecTag, "hvc1");
    EXPECT_EQ(back.value().pixelFormat, "yuv420p10le");
    EXPECT_FALSE(back.value().builtIn);

    struct Case {
        std::string json;
        std::string expect;
    };
    const Case cases[] = {
        {R"({"extension": ".mp4"})", "required field"},
        {R"({"id": "bad id!", "extension": ".mp4", "videoCodec": "libx264"})", "letters, digits"},
        {R"({"id": "x", "extension": ".avi", "videoCodec": "libx264"})", "unsupported extension"},
        {R"({"id": "x", "extension": ".mp4", "videoCodec": "libx264", "pixelFormat": "yuv999"})", "unknown pixel format"},
        {R"({"id": "x", "extension": ".mp4", "videoCodec": "libx264", "crf": 80})", "crf"},
        {R"({"id": "x", "extension": ".wav", "videoCodec": "libx264"})", "cannot contain video"},
        {R"({"id": "x", "extension": ".png", "videoCodec": "png", "audio": true})", "cannot contain audio"},
        {R"({"id": "x", "extension": ".mp4", "video": false, "audio": false})", "neither video nor audio"},
        {R"({"id": "x", "extension": ".mp4", "videoCodec": "libx264", "codecTag": "toolong"})", "four characters"},
        {"not json", "not valid JSON"},
    };
    for (const auto& c : cases) {
        auto r = presetFromJson(c.json);
        ASSERT_FALSE(r.ok()) << c.json;
        EXPECT_NE(r.error().message.find(c.expect), std::string::npos) << r.error().message;
    }
    ExportPreset missing = p;
    missing.videoCodec = "no_such_encoder";
    EXPECT_NE(presetUnavailableReason(missing).find("no_such_encoder"), std::string::npos);
    EXPECT_TRUE(presetUnavailableReason(*findPreset(builtInPresets(), "wav-24")).empty());
}

TEST(Presets, UserPresetFolder) {
    test::TempDir dir;
    ExportPreset mine = *findPreset(builtInPresets(), "h264-web");
    mine.id = "social-9x16";
    mine.name = "Social";
    test::writeText(dir / "a-social.json", presetToJson(mine));
    test::writeText(dir / "b-broken.json", "{ nope");
    ExportPreset clash = mine;
    clash.id = "h264-web";
    test::writeText(dir / "c-clash.json", presetToJson(clash));
    test::writeText(dir / "notes.txt", "ignored");
    std::vector<Error> problems;
    const auto presets = loadPresets(dir.path(), &problems);
    EXPECT_EQ(presets.size(), builtInPresets().size() + 1);
    ASSERT_NE(findPreset(presets, "social-9x16"), nullptr);
    EXPECT_FALSE(findPreset(presets, "social-9x16")->builtIn);
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_NE(problems[0].message.find("b-broken.json"), std::string::npos);
    EXPECT_NE(problems[1].message.find("already used"), std::string::npos);
    EXPECT_EQ(loadPresets(dir / "missing").size(), builtInPresets().size());
}
