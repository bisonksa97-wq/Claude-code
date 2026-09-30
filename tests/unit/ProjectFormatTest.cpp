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

TEST(ProjectFormat, RoundTripsMarksAndTargets) {
    Project p = sampleProject();
    p.media[0].markIn = 1.5;
    p.media[0].markOut = 3.25;
    p.timelines[0].markIn = 12;
    p.timelines[0].markOut = 48;
    p.timelines[0].audioTarget = p.timelines[0].tracks[3].id;  // A2
    auto q = ProjectSerializer::fromJson(ProjectSerializer::toJson(p));
    ASSERT_TRUE(q.ok()) << q.error().toString();
    EXPECT_EQ(q.value().media[0].markIn, 1.5);
    EXPECT_EQ(q.value().media[0].markOut, 3.25);
    EXPECT_EQ(q.value().timelines[0].markIn, 12);
    EXPECT_EQ(q.value().timelines[0].markOut, 48);
    EXPECT_EQ(q.value().timelines[0].videoTarget, p.timelines[0].tracks[0].id);
    EXPECT_EQ(q.value().timelines[0].audioTarget, p.timelines[0].tracks[3].id);

    p.timelines[0].markIn.reset();
    p.timelines[0].videoTarget.clear();  // a disabled target survives the round trip
    q = ProjectSerializer::fromJson(ProjectSerializer::toJson(p));
    ASSERT_TRUE(q.ok());
    EXPECT_FALSE(q.value().timelines[0].markIn.has_value());
    EXPECT_TRUE(q.value().timelines[0].videoTarget.empty());
}

TEST(ProjectFormat, RejectsTargetsOfTheWrongKind) {
    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    doc["timelines"][0]["targets"]["video"] = doc["timelines"][0]["tracks"][2]["id"];  // an audio track
    EXPECT_FALSE(ProjectSerializer::fromJson(doc.dump()).ok());
}

// A document exactly as format version 1 wrote it (no marks, no targets).
TEST(ProjectFormat, MigratesVersion1Documents) {
    const char* v1 = R"({
      "format": "ultimatepost.project", "formatVersion": 1,
      "project": {"id": "p1", "name": "Old", "createdAt": "", "modifiedAt": "",
                  "settings": {"frameRate": "25/1", "width": 1920, "height": 1080, "sampleRate": 48000},
                  "activeTimelineId": "t1"},
      "bins": [{"id": "b1", "name": "Master", "parentId": ""}],
      "media": [{"id": "m1", "name": "a.mov", "path": "/a.mov", "relativePath": "", "binId": "b1",
                 "info": {"hasVideo": true, "frameRate": "25/1", "durationSeconds": 4.0},
                 "rating": 0, "keywords": [], "comment": "", "importedAt": ""}],
      "timelines": [{"id": "t1", "name": "Timeline 1", "frameRate": "25/1", "width": 1920, "height": 1080,
                     "sampleRate": 48000, "tracks": [
                       {"id": "a1", "kind": "audio", "name": "A1", "clips": []},
                       {"id": "v1", "kind": "video", "name": "V1", "clips": []},
                       {"id": "v2", "kind": "video", "name": "V2", "clips": []}]}]
    })";
    auto p = ProjectSerializer::fromJson(v1);
    ASSERT_TRUE(p.ok()) << p.error().toString();
    const Timeline& t = p.value().timelines[0];
    EXPECT_EQ(t.videoTarget, "v1");  // first track of each kind
    EXPECT_EQ(t.audioTarget, "a1");
    EXPECT_FALSE(t.markIn.has_value());
    EXPECT_FALSE(p.value().media[0].markIn.has_value());
    // Saving writes the current version.
    EXPECT_EQ(nlohmann::json::parse(ProjectSerializer::toJson(p.value()))["formatVersion"], Project::kFormatVersion);
}

TEST(ProjectFormat, RoundTripsMarkers) {
    Project p = sampleProject();
    p.timelines[0].markers.push_back(Marker{"tm", 12, 5, "Scene 2", "check sync", MarkerColor::Yellow});
    p.timelines[0].tracks[0].clips[0].markers.push_back(Marker{"cm", 30, 0, "Beat", "", MarkerColor::Red});
    auto q = ProjectSerializer::fromJson(ProjectSerializer::toJson(p));
    ASSERT_TRUE(q.ok()) << q.error().toString();
    const Marker& tm = q.value().timelines[0].markers.at(0);
    EXPECT_EQ(tm.frame, 12);
    EXPECT_EQ(tm.duration, 5);
    EXPECT_EQ(tm.name, "Scene 2");
    EXPECT_EQ(tm.comment, "check sync");
    EXPECT_EQ(tm.color, MarkerColor::Yellow);
    EXPECT_EQ(q.value().timelines[0].tracks[0].clips[0].markers.at(0).name, "Beat");
}

// A document exactly as format version 2 wrote it (no markers anywhere).
TEST(ProjectFormat, MigratesVersion2Documents) {
    const char* v2 = R"({
      "format": "ultimatepost.project", "formatVersion": 2,
      "project": {"id": "p2", "name": "V2", "createdAt": "", "modifiedAt": "",
                  "settings": {"frameRate": "25/1", "width": 1920, "height": 1080, "sampleRate": 48000},
                  "activeTimelineId": "t1"},
      "bins": [],
      "media": [{"id": "m1", "name": "a.mov", "path": "/a.mov", "relativePath": "", "binId": "",
                 "info": {"hasVideo": true, "frameRate": "25/1", "durationSeconds": 4.0},
                 "rating": 0, "keywords": [], "comment": "", "importedAt": "", "markIn": 1.0, "markOut": null}],
      "timelines": [{"id": "t1", "name": "Timeline 1", "frameRate": "25/1", "width": 1920, "height": 1080,
                     "sampleRate": 48000, "markIn": 5, "markOut": null,
                     "targets": {"video": "v1", "audio": ""},
                     "tracks": [{"id": "v1", "kind": "video", "name": "V1", "clips": [
                        {"id": "c1", "mediaId": "m1", "name": "a", "start": 0, "duration": 50, "sourceIn": 0,
                         "sourceLength": 100, "linkId": "", "enabled": true, "gainDb": 0}]}]}]
    })";
    auto p = ProjectSerializer::fromJson(v2);
    ASSERT_TRUE(p.ok()) << p.error().toString();
    EXPECT_TRUE(p.value().timelines[0].markers.empty());
    EXPECT_TRUE(p.value().timelines[0].tracks[0].clips[0].markers.empty());
    EXPECT_EQ(p.value().timelines[0].markIn, 5);  // v2 data is preserved
    EXPECT_EQ(p.value().media[0].markIn, 1.0);
}

TEST(ProjectFormat, RoundTripsTransformsAndOmitsDefaults) {
    Project p = sampleProject();
    Clip& c = p.timelines[0].tracks[0].clips[0];
    c.transform[ClipParam::Scale].value = 50;
    c.transform[ClipParam::Opacity].setKey(5, 0, Interpolation::EaseInOut);
    c.transform[ClipParam::Opacity].setKey(30, 100);
    const std::string text = ProjectSerializer::toJson(p);
    auto doc = nlohmann::json::parse(text);
    const auto& tj = doc["timelines"][0]["tracks"][0]["clips"][0]["transform"];
    EXPECT_TRUE(tj.contains("scale"));
    EXPECT_FALSE(tj.contains("rotation"));  // defaults are not written
    auto q = ProjectSerializer::fromJson(text);
    ASSERT_TRUE(q.ok()) << q.error().toString();
    const ClipTransform& t = q.value().timelines[0].tracks[0].clips[0].transform;
    EXPECT_EQ(t[ClipParam::Scale].value, 50);
    ASSERT_EQ(t[ClipParam::Opacity].keys.size(), 2u);
    EXPECT_EQ(t[ClipParam::Opacity].keys[0].interpolation, Interpolation::EaseInOut);
    EXPECT_EQ(t[ClipParam::Rotation].value, 0);
}

TEST(ProjectFormat, MigratesVersion3Documents) {
    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    doc["formatVersion"] = 3;
    for (auto& clip : doc["timelines"][0]["tracks"][0]["clips"]) clip.erase("transform");
    auto p = ProjectSerializer::fromJson(doc.dump());
    ASSERT_TRUE(p.ok()) << p.error().toString();
    EXPECT_TRUE(p.value().timelines[0].tracks[0].clips[0].transform.isIdentity());
}

TEST(ProjectFormat, RoundTripsTransitionsAndMigratesV4) {
    Project p = sampleProject();
    Clip& c = p.timelines[0].tracks[0].clips[0];
    c.transitionIn = Transition{TransitionKind::Dip, 12, TransitionAlignment::StartAtCut};
    auto q = ProjectSerializer::fromJson(ProjectSerializer::toJson(p));
    ASSERT_TRUE(q.ok()) << q.error().toString();
    const Clip& qc = q.value().timelines[0].tracks[0].clips[0];
    ASSERT_TRUE(qc.transitionIn.has_value());
    EXPECT_EQ(qc.transitionIn->kind, TransitionKind::Dip);
    EXPECT_EQ(qc.transitionIn->duration, 12);
    EXPECT_EQ(qc.transitionIn->alignment, TransitionAlignment::StartAtCut);
    EXPECT_FALSE(qc.transitionOut.has_value());

    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    doc["formatVersion"] = 4;
    for (auto& clip : doc["timelines"][0]["tracks"][0]["clips"]) {
        clip.erase("transitionIn");
        clip.erase("transitionOut");
    }
    auto migrated = ProjectSerializer::fromJson(doc.dump());
    ASSERT_TRUE(migrated.ok()) << migrated.error().toString();
    EXPECT_FALSE(migrated.value().timelines[0].tracks[0].clips[0].transitionIn.has_value());
}

TEST(ProjectFormat, RoundTripsTrackAudioAndMigratesV5) {
    Project p = sampleProject();
    Track& a1 = p.timelines[0].tracks[2];
    a1.pan = -0.25;
    auto eq = audio::makeEffect("eq3");
    ASSERT_TRUE(eq.ok());
    eq.value().params["lowGain"] = 3;
    eq.value().enabled = false;
    a1.effects.push_back(eq.value());
    p.timelines[0].tracks[0].clips[0].transform[ClipParam::Volume].setKey(4, -12);
    auto q = ProjectSerializer::fromJson(ProjectSerializer::toJson(p));
    ASSERT_TRUE(q.ok()) << q.error().toString();
    const Track& qa = q.value().timelines[0].tracks[2];
    EXPECT_DOUBLE_EQ(qa.pan, -0.25);
    ASSERT_EQ(qa.effects.size(), 1u);
    EXPECT_EQ(qa.effects[0].type, "eq3");
    EXPECT_FALSE(qa.effects[0].enabled);
    EXPECT_EQ(qa.effects[0].param("lowGain"), 3);
    EXPECT_EQ(q.value().timelines[0].tracks[0].clips[0].transform[ClipParam::Volume].keys.size(), 1u);

    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    doc["formatVersion"] = 5;
    for (auto& tr : doc["timelines"][0]["tracks"]) {
        tr.erase("pan");
        tr.erase("effects");
    }
    auto migrated = ProjectSerializer::fromJson(doc.dump());
    ASSERT_TRUE(migrated.ok()) << migrated.error().toString();
    EXPECT_TRUE(migrated.value().timelines[0].tracks[2].effects.empty());
    // An invalid effect parameter in a file is rejected on load.
    auto broken = nlohmann::json::parse(ProjectSerializer::toJson(p));
    broken["timelines"][0]["tracks"][2]["effects"][0]["params"]["midQ"] = 99;
    EXPECT_FALSE(ProjectSerializer::fromJson(broken.dump()).ok());
}

TEST(ProjectFormat, MigratesV6ToGrades) {
    auto doc = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    doc["formatVersion"] = 6;
    for (auto& clip : doc["timelines"][0]["tracks"][0]["clips"]) clip.erase("grade");
    auto p = ProjectSerializer::fromJson(doc.dump());
    ASSERT_TRUE(p.ok()) << p.error().toString();
    EXPECT_TRUE(p.value().timelines[0].tracks[0].clips[0].grade.isIdentity());
    // Only non-default grade values are written.
    Project q = sampleProject();
    q.timelines[0].tracks[0].clips[0].grade[GradeParam::LiftB].value = 0.05;
    auto written = nlohmann::json::parse(ProjectSerializer::toJson(q));
    const auto& grade = written["timelines"][0]["tracks"][0]["clips"][0]["grade"];
    EXPECT_EQ(grade.size(), 1u);
    EXPECT_DOUBLE_EQ(grade["liftB"]["value"].get<double>(), 0.05);
}

TEST(ProjectFormat, RoundTripsCurvesLutsVersionsAndMigratesV7) {
    test::TempDir dir;
    test::writeText(dir / "luts" / "look.cube", "LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    Project p = sampleProject();
    Timeline& tl = p.timelines[0];
    Clip& clip = tl.tracks[0].clips[0];
    clip.grade.curve(CurveKind::Master) = {{0.0, 0.1}, {1.0, 0.9}};
    clip.grade.curve(CurveKind::HueVsSat) = {{0.3, 0.2}};
    clip.grade.lut = LutRef{dir / "luts" / "look.cube", {}};
    clip.gradeBypass = true;
    clip.gradeVersion = "B";
    ClipGrade a;
    a[GradeParam::Saturation].value = 0.5;
    clip.gradeVersions.push_back(NamedGrade{"A", a});
    tl.outputLut = LutRef{dir / "luts" / "look.cube", {}};
    tl.gradesBypassed = true;

    const auto file = dir / "proj" / "p.uproj";
    std::filesystem::create_directories(file.parent_path());
    ASSERT_TRUE(ProjectSerializer::save(p, file).ok());
    auto doc = nlohmann::json::parse(readFile(file).value());
    const auto& cj = doc["timelines"][0]["tracks"][0]["clips"][0];
    EXPECT_EQ(cj["grade"]["lut"]["relativePath"], "../luts/look.cube");
    EXPECT_EQ(cj["grade"]["curves"]["master"].size(), 2u);
    EXPECT_FALSE(cj["grade"]["curves"].contains("red"));  // empty curves are omitted
    EXPECT_EQ(doc["timelines"][0]["outputLut"]["relativePath"], "../luts/look.cube");

    auto loaded = ProjectSerializer::load(file);
    ASSERT_TRUE(loaded.ok()) << loaded.error().toString();
    const Timeline& lt = loaded.value().timelines[0];
    const Clip& lc = lt.tracks[0].clips[0];
    EXPECT_EQ(lc.grade.curve(CurveKind::Master), clip.grade.curve(CurveKind::Master));
    EXPECT_EQ(lc.grade.curve(CurveKind::HueVsSat), clip.grade.curve(CurveKind::HueVsSat));
    ASSERT_TRUE(lc.grade.lut.has_value());
    EXPECT_EQ(lc.grade.lut->path, dir / "luts" / "look.cube");
    EXPECT_TRUE(lc.gradeBypass);
    EXPECT_EQ(lc.gradeVersion, "B");
    ASSERT_EQ(lc.gradeVersions.size(), 1u);
    EXPECT_EQ(lc.gradeVersions[0].name, "A");
    EXPECT_DOUBLE_EQ(lc.gradeVersions[0].grade[GradeParam::Saturation].value, 0.5);
    EXPECT_TRUE(lt.outputLut.has_value());
    EXPECT_TRUE(lt.gradesBypassed);

    // Moving the whole folder keeps the LUTs through their relative paths.
    const auto moved = dir.path().parent_path() / (dir.path().filename().string() + "-moved");
    std::filesystem::rename(dir.path(), moved);
    auto relocated = ProjectSerializer::load(moved / "proj" / "p.uproj");
    std::filesystem::rename(moved, dir.path());
    ASSERT_TRUE(relocated.ok());
    EXPECT_EQ(relocated.value().timelines[0].tracks[0].clips[0].grade.lut->path, (moved / "luts" / "look.cube").lexically_normal());

    // Invalid curves are rejected on load.
    auto bad = doc;
    bad["timelines"][0]["tracks"][0]["clips"][0]["grade"]["curves"]["red"] = nlohmann::json::array({nlohmann::json::array({0.5, 2.0})});
    EXPECT_FALSE(ProjectSerializer::fromJson(bad.dump()).ok());
    auto dup = doc;
    dup["timelines"][0]["tracks"][0]["clips"][0]["gradeVersions"][0]["name"] = "B";
    EXPECT_FALSE(ProjectSerializer::fromJson(dup.dump()).ok());

    // v7 documents have none of the new keys.
    auto old = nlohmann::json::parse(ProjectSerializer::toJson(sampleProject()));
    old["formatVersion"] = 7;
    old["timelines"][0].erase("outputLut");
    old["timelines"][0].erase("gradesBypassed");
    for (auto& c : old["timelines"][0]["tracks"][0]["clips"]) {
        c.erase("gradeBypass");
        c.erase("gradeVersion");
        c.erase("gradeVersions");
    }
    auto migrated = ProjectSerializer::fromJson(old.dump());
    ASSERT_TRUE(migrated.ok()) << migrated.error().toString();
    const Clip& mc = migrated.value().timelines[0].tracks[0].clips[0];
    EXPECT_EQ(mc.gradeVersion, "A");
    EXPECT_TRUE(mc.gradeVersions.empty());
    EXPECT_FALSE(mc.gradeBypass);
    EXPECT_FALSE(migrated.value().timelines[0].outputLut.has_value());
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
