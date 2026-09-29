#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "core/AtomicFile.h"
#include "project/ProjectMigrator.h"
#include "project/ProjectSerializer.h"
#include "support/TestSupport.h"
#include "timeline/EditOperations.h"

using namespace up;

namespace {

Project sampleProject() {
    Project p = Project::create("Sample", SequenceSettings{FrameRate{30000, 1001}, 1280, 720, 48000});
    MediaItem m;
    m.id = "media-1";
    m.name = "shot01.mov";
    m.path = "/footage/day1/shot01.mov";
    m.binId = p.bins.front().id;
    m.info.hasVideo = true;
    m.info.width = 3840;
    m.info.height = 2160;
    m.info.frameRate = FrameRate{24000, 1001};
    m.info.durationSeconds = 12.5;
    m.info.hasAudio = true;
    m.info.sampleRate = 48000;
    m.info.channels = 2;
    m.rating = 4;
    m.keywords = {"interview", "day1"};
    m.comment = "Good take";
    p.media.push_back(m);
    Timeline& tl = p.timelines.front();
    Clip c;
    c.mediaId = m.id;
    c.name = m.name;
    c.start = 10;
    c.duration = 100;
    c.sourceIn = 5;
    c.sourceLength = 300;
    c.linkId = "L1";
    c.gainDb = -3.5;
    EXPECT_TRUE(ops::placeClip(tl, tl.tracks[0].id, c, ops::EditMode::Overwrite).ok());
    tl.tracks[2].muted = true;
    tl.tracks[2].gainDb = -6;
    return p;
}

}  // namespace

TEST(ProjectFormat, RoundTripsAllFields) {
    const Project p = sampleProject();
    const std::string text = ProjectSerializer::toJson(p);
    auto loaded = ProjectSerializer::fromJson(text);
    ASSERT_TRUE(loaded.ok()) << loaded.error().toString();
    const Project& q = loaded.value();
    EXPECT_EQ(q.id, p.id);
    EXPECT_EQ(q.settings.frameRate, (FrameRate{30000, 1001}));
    ASSERT_EQ(q.media.size(), 1u);
    EXPECT_EQ(q.media[0].path, p.media[0].path);
    EXPECT_EQ(q.media[0].info.frameRate, (FrameRate{24000, 1001}));
    EXPECT_EQ(q.media[0].keywords, p.media[0].keywords);
    EXPECT_EQ(q.media[0].rating, 4);
    const Clip& c = q.timelines[0].tracks[0].clips.at(0);
    EXPECT_EQ(c.start, 10);
    EXPECT_EQ(c.sourceIn, 5);
    EXPECT_EQ(c.linkId, "L1");
    EXPECT_DOUBLE_EQ(c.gainDb, -3.5);
    EXPECT_TRUE(q.timelines[0].tracks[2].muted);
    // Serialisation is deterministic.
    EXPECT_EQ(ProjectSerializer::toJson(q), text);
}

TEST(ProjectFormat, RejectsNewerVersions) {
    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    doc["formatVersion"] = Project::kFormatVersion + 1;
    auto r = ProjectSerializer::fromJson(doc.dump());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::UnsupportedVersion);
}

TEST(ProjectFormat, RejectsDamagedFiles) {
    EXPECT_EQ(ProjectSerializer::fromJson("{not json").error().code, ErrorCode::ParseError);
    EXPECT_EQ(ProjectSerializer::fromJson("{}").error().code, ErrorCode::ParseError);
    EXPECT_EQ(ProjectSerializer::fromJson(R"({"formatVersion":1})").error().code, ErrorCode::ParseError);
}

TEST(ProjectFormat, RejectsStructurallyInvalidTimelines) {
    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    auto& clips = doc["timelines"][0]["tracks"][0]["clips"];
    auto overlapping = clips[0];
    overlapping["id"] = "other";
    overlapping["start"] = 50;
    clips.push_back(overlapping);
    auto r = ProjectSerializer::fromJson(doc.dump());
    ASSERT_FALSE(r.ok());
}

TEST(ProjectMigrator, AppliesStepsInOrder) {
    ProjectMigrator m(3);
    m.addStep(1, [](nlohmann::json& d) {
        d["renamed"] = d["old"];
        d.erase("old");
        return Status::success();
    });
    m.addStep(2, [](nlohmann::json& d) {
        d["renamed"] = d["renamed"].get<int>() * 10;
        return Status::success();
    });
    nlohmann::json doc{{"formatVersion", 1}, {"old", 4}};
    ASSERT_TRUE(m.migrate(doc).ok());
    EXPECT_EQ(doc["formatVersion"], 3);
    EXPECT_EQ(doc["renamed"], 40);
    EXPECT_FALSE(doc.contains("old"));
}

TEST(ProjectMigrator, ReportsMissingUpgradePath) {
    ProjectMigrator m(3);
    nlohmann::json doc{{"formatVersion", 1}};
    EXPECT_EQ(m.migrate(doc).error().code, ErrorCode::UnsupportedVersion);
}

TEST(ProjectFormat, SaveLoadFileWithRelativeMediaPaths) {
    test::TempDir dir;
    Project p = sampleProject();
    p.media[0].path = dir / "media" / "shot01.mov";
    const auto file = dir / "proj" / "p.uproj";
    std::filesystem::create_directories(file.parent_path());
    ASSERT_TRUE(ProjectSerializer::save(p, file).ok());
    auto doc = nlohmann::json::parse(readFile(file).value());
    EXPECT_EQ(doc["media"][0]["relativePath"], "../media/shot01.mov");
    auto loaded = ProjectSerializer::load(file);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value().filePath, file);
    EXPECT_EQ(loaded.value().media[0].relativePath, std::filesystem::path("../media/shot01.mov"));
}
