#include <gtest/gtest.h>

#include "app/EditorSession.h"
#include "support/TestSupport.h"

using namespace up;
namespace fs = std::filesystem;

namespace {

class SessionTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dir = new test::TempDir();
        ASSERT_TRUE(media::generateSyntheticMedia(*dir / "a.mp4", test::solid(200, 0, 0, 50)).ok());
        ASSERT_TRUE(media::generateSyntheticMedia(*dir / "b.mp4", test::solid(0, 0, 200, 40)).ok());
        media::SyntheticSpec audioOnly = test::solid(0, 0, 0, 25);
        ASSERT_TRUE(media::generateSyntheticMedia(*dir / "short.mp4", audioOnly).ok());
    }
    static void TearDownTestSuite() {
        delete dir;
        dir = nullptr;
    }

    void SetUp() override {
        session = EditorSession::createNew("Test", SequenceSettings{FrameRate{25, 1}, 320, 240, 48000});
        auto report = session->importMedia({*dir / "a.mp4", *dir / "b.mp4"});
        ASSERT_EQ(report.importedIds.size(), 2u);
        a = report.importedIds[0];
        b = report.importedIds[1];
    }

    const Track& track(std::size_t i) { return session->timeline().tracks[i]; }

    static test::TempDir* dir;
    std::unique_ptr<EditorSession> session;
    std::string a, b;
};

test::TempDir* SessionTest::dir = nullptr;

}  // namespace

TEST_F(SessionTest, ImportReportsFailuresWithoutAbortingBatch) {
    auto report = session->importMedia({*dir / "missing.mov", *dir / "a.mp4"});
    EXPECT_EQ(report.importedIds.size(), 1u);
    ASSERT_EQ(report.failures.size(), 1u);
    EXPECT_EQ(report.failures[0].code, ErrorCode::NotFound);
}

TEST_F(SessionTest, AppendCreatesLinkedVideoAndAudio) {
    auto r = session->appendMedia(a);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 2u);
    const auto& tl = session->timeline();
    EXPECT_EQ(tl.clip(r.value()[0])->linkId, tl.clip(r.value()[1])->linkId);
    EXPECT_EQ(tl.duration(), 50);
    ASSERT_TRUE(session->appendMedia(b).ok());
    EXPECT_EQ(tl.duration(), 90);
    EXPECT_EQ(track(0).clips.size(), 2u);
    EXPECT_EQ(track(2).clips.size(), 2u);
}

TEST_F(SessionTest, InsertKeepsAllTracksInSync) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    ASSERT_TRUE(session->placeMedia(b, 20, ops::EditMode::Insert).ok());
    // Video and audio both: [0,20) a, [20,60) b, [60,90) rest of a.
    for (std::size_t i : {0u, 2u}) {
        ASSERT_EQ(track(i).clips.size(), 3u);
        EXPECT_EQ(track(i).clips[1].start, 20);
        EXPECT_EQ(track(i).clips[2].start, 60);
        EXPECT_EQ(track(i).clips[2].sourceIn, 20);
    }
}

TEST_F(SessionTest, RazorCutsLinkedClipsAndKeepsThemLinked) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    auto cuts = session->razorAt(20);
    ASSERT_TRUE(cuts.ok());
    EXPECT_EQ(cuts.value(), 2);
    const auto& v = track(0).clips;
    const auto& au = track(2).clips;
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].linkId, au[0].linkId);
    EXPECT_EQ(v[1].linkId, au[1].linkId);
    EXPECT_NE(v[0].linkId, v[1].linkId);
    EXPECT_FALSE(session->razorAt(20).ok());  // nothing strictly inside at a cut point
}

TEST_F(SessionTest, LinkedEditsMoveTogetherAndUndoAsOneStep) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    ASSERT_TRUE(session->appendMedia(b).ok());
    const std::string first = track(0).clips[0].id;
    ASSERT_TRUE(session->trimClip(first, ops::Edge::Out, -10, ops::TrimMode::Ripple).ok());
    EXPECT_EQ(track(0).clips[1].start, 40);
    EXPECT_EQ(track(2).clips[1].start, 40);
    ASSERT_TRUE(session->undo());
    EXPECT_EQ(track(0).clips[1].start, 50);
    EXPECT_EQ(track(2).clips[1].start, 50);
    ASSERT_TRUE(session->redo());
    EXPECT_EQ(track(2).clips[1].start, 40);

    ASSERT_TRUE(session->rippleDeleteClip(first).ok());
    EXPECT_EQ(track(0).clips.size(), 1u);
    EXPECT_EQ(track(2).clips.size(), 1u);
    EXPECT_EQ(track(2).clips[0].start, 0);
}

TEST_F(SessionTest, MoveClipCarriesLinkedPartner) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    const std::string v = track(0).clips[0].id;
    ASSERT_TRUE(session->moveClip(v, track(1).id, 30).ok());
    EXPECT_TRUE(track(0).clips.empty());
    ASSERT_EQ(track(1).clips.size(), 1u);
    EXPECT_EQ(track(1).clips[0].start, 30);
    EXPECT_EQ(track(2).clips[0].start, 30);
}

TEST_F(SessionTest, LockedTrackIsSkippedByLinkedEditsAndRazor) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    const Track& a1 = track(2);
    TrackState locked;
    locked.locked = true;
    ASSERT_TRUE(session->setTrackState(a1.id, locked).ok());
    ASSERT_TRUE(session->razorAt(10).ok());
    EXPECT_EQ(track(0).clips.size(), 2u);
    EXPECT_EQ(track(2).clips.size(), 1u);
}

TEST_F(SessionTest, DirtyTrackingFollowsSaves) {
    EXPECT_FALSE(EditorSession::createNew("Fresh")->isDirty());
    ASSERT_TRUE(session->appendMedia(a).ok());
    EXPECT_TRUE(session->isDirty());
    test::TempDir tmp;
    ASSERT_TRUE(session->saveAs(tmp / "p.uproj").ok());
    EXPECT_FALSE(session->isDirty());
    session->undo();
    EXPECT_TRUE(session->isDirty());
}

TEST_F(SessionTest, OfflineDetectionAndRelink) {
    test::TempDir tmp;
    fs::copy_file(*dir / "a.mp4", tmp / "moved.mp4");
    auto s = EditorSession::createNew("Relink");
    auto report = s->importMedia({tmp / "moved.mp4"});
    ASSERT_EQ(report.importedIds.size(), 1u);
    const std::string id = report.importedIds[0];
    ASSERT_TRUE(s->saveAs(tmp / "p.uproj").ok());

    fs::rename(tmp / "moved.mp4", tmp / "elsewhere.mp4");
    auto reopened = EditorSession::open(tmp / "p.uproj");
    ASSERT_TRUE(reopened.ok());
    EXPECT_FALSE(reopened.value()->project().findMedia(id)->online);

    // A shorter file is rejected as a relink candidate.
    auto bad = reopened.value()->relinkMedia(id, *dir / "short.mp4");
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, ErrorCode::Conflict);

    ASSERT_TRUE(reopened.value()->relinkMedia(id, tmp / "elsewhere.mp4").ok());
    EXPECT_TRUE(reopened.value()->project().findMedia(id)->online);
    reopened.value()->undo();
    EXPECT_FALSE(reopened.value()->project().findMedia(id)->online);
}

TEST_F(SessionTest, MovedProjectFolderFindsMediaByRelativePath) {
    test::TempDir tmp;
    fs::create_directories(tmp / "job" / "media");
    fs::copy_file(*dir / "a.mp4", tmp / "job" / "media" / "a.mp4");
    auto s = EditorSession::createNew("Portable");
    ASSERT_EQ(s->importMedia({tmp / "job" / "media" / "a.mp4"}).importedIds.size(), 1u);
    ASSERT_TRUE(s->saveAs(tmp / "job" / "p.uproj").ok());
    fs::rename(tmp / "job", tmp / "job-moved");
    auto reopened = EditorSession::open(tmp / "job-moved" / "p.uproj");
    ASSERT_TRUE(reopened.ok());
    EXPECT_TRUE(reopened.value()->project().media[0].online);
    EXPECT_EQ(reopened.value()->project().media[0].path, tmp / "job-moved" / "media" / "a.mp4");
}

TEST_F(SessionTest, AutosaveAndRecovery) {
    test::TempDir tmp;
    ASSERT_TRUE(session->saveAs(tmp / "p.uproj").ok());
    ASSERT_TRUE(session->appendMedia(a).ok());
    EXPECT_FALSE(EditorSession::newerAutosaveFor(tmp / "p.uproj").has_value());
    ASSERT_TRUE(session->writeAutosave().ok());
    // Make sure the autosave is strictly newer than the project on coarse-timestamp file systems.
    fs::last_write_time(tmp / "p.uproj", fs::last_write_time(tmp / "p.uproj") - std::chrono::seconds(5));
    auto autosave = EditorSession::newerAutosaveFor(tmp / "p.uproj");
    ASSERT_TRUE(autosave.has_value());

    auto recovered = EditorSession::openRecovery(*autosave, tmp / "p.uproj");
    ASSERT_TRUE(recovered.ok());
    EXPECT_TRUE(recovered.value()->isDirty());
    EXPECT_EQ(recovered.value()->timeline().duration(), 50);
    ASSERT_TRUE(recovered.value()->save().ok());
    EXPECT_FALSE(fs::exists(*autosave));  // saving clears the autosave
}

TEST_F(SessionTest, ThreePointOverwriteUsesMarksAndClearsTimelineMarks) {
    ASSERT_TRUE(session->appendMedia(a).ok());  // a: 0..50 on V1/A1
    ASSERT_TRUE(session->setMediaMarks(b, 10, 20).ok());
    ASSERT_TRUE(session->setTimelineMarks(5, std::nullopt).ok());
    auto r = session->threePointEdit(b, ops::EditMode::Overwrite, 30);
    ASSERT_TRUE(r.ok()) << r.error().toString();
    EXPECT_EQ(r.value().recordIn, 5);
    EXPECT_EQ(r.value().recordOut, 15);
    ASSERT_EQ(r.value().clipIds.size(), 2u);
    const Clip* placed = session->timeline().clip(r.value().clipIds[0]);
    EXPECT_EQ(placed->start, 5);
    EXPECT_EQ(placed->sourceIn, 10);
    EXPECT_EQ(placed->duration, 10);
    EXPECT_EQ(track(0).clips.size(), 3u);  // a split around the overwrite
    EXPECT_FALSE(session->timeline().markIn.has_value());

    // The whole edit, including clearing the marks, is one undo step.
    ASSERT_TRUE(session->undo());
    EXPECT_EQ(track(0).clips.size(), 1u);
    EXPECT_EQ(session->timeline().markIn, 5);
}

TEST_F(SessionTest, ThreePointInsertBacktimesAndHonoursTargets) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    const std::string v2 = track(1).id;
    ASSERT_TRUE(session->setTrackTargets(v2, "").ok());  // video to V2, audio disabled
    ASSERT_TRUE(session->setMediaMarks(b, std::nullopt, 25).ok());
    ASSERT_TRUE(session->setTimelineMarks(std::nullopt, 40).ok());
    auto r = session->threePointEdit(b, ops::EditMode::Insert, 0);
    ASSERT_TRUE(r.ok()) << r.error().toString();
    EXPECT_EQ(r.value().recordIn, 15);
    ASSERT_EQ(r.value().clipIds.size(), 1u);  // no audio: target disabled
    EXPECT_EQ(session->timeline().trackOfClip(r.value().clipIds[0])->id, v2);
    // Insert rippled the unlocked tracks: a's second half moved right by 25.
    ASSERT_EQ(track(0).clips.size(), 2u);
    EXPECT_EQ(track(0).clips[1].start, 40);
    EXPECT_EQ(track(2).clips[1].start, 40);
}

TEST_F(SessionTest, ThreePointEditExplainsMissingTargetsAndBadMarks) {
    ASSERT_TRUE(session->setTrackTargets("", "").ok());
    auto r = session->threePointEdit(a, ops::EditMode::Overwrite, 0);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().suggestion.find("target"), std::string::npos);
    EXPECT_FALSE(session->setTrackTargets(track(2).id, "").ok());  // audio track as video target
    EXPECT_FALSE(session->setMediaMarks(a, 30, 10).ok());
    EXPECT_FALSE(session->setMediaMarks(a, 0, 500).ok());  // beyond the media
    EXPECT_FALSE(session->setTimelineMarks(20, 10).ok());
}

TEST_F(SessionTest, MarksPersistAndUndo) {
    test::TempDir tmp;
    ASSERT_TRUE(session->setMediaMarks(a, 3, 17).ok());
    ASSERT_TRUE(session->setTimelineMarks(2, 9).ok());
    ASSERT_TRUE(session->saveAs(tmp / "p.uproj").ok());
    auto reopened = EditorSession::open(tmp / "p.uproj");
    ASSERT_TRUE(reopened.ok());
    const auto [in, out] = reopened.value()->mediaMarks(a);
    EXPECT_EQ(in, 3);
    EXPECT_EQ(out, 17);
    EXPECT_EQ(reopened.value()->timeline().markOut, 9);
    ASSERT_TRUE(reopened.value()->setMediaMarks(a, std::nullopt, std::nullopt).ok());
    EXPECT_FALSE(reopened.value()->mediaMarks(a).first.has_value());
    reopened.value()->undo();
    EXPECT_EQ(reopened.value()->mediaMarks(a).first, 3);
}

TEST_F(SessionTest, MarkersAddEditNavigateAndUndo) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    auto m1 = session->addMarker(30, "Act 2", MarkerColor::Green);
    ASSERT_TRUE(m1.ok());
    auto m2 = session->addMarker(10);
    ASSERT_TRUE(m2.ok());
    const std::string clip = track(0).clips[0].id;
    auto cm = session->addClipMarker(clip, 20, "Beat");
    ASSERT_TRUE(cm.ok());
    EXPECT_FALSE(session->addClipMarker(clip, 70).ok());  // outside the clip

    const auto all = session->markers();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].timelineFrame, 10);
    EXPECT_EQ(all[1].clipId, clip);
    EXPECT_EQ(all[2].marker.name, "Act 2");
    EXPECT_EQ(session->timeline().nextMarker(10), 20);

    Marker edited = session->findMarker(m1.value())->marker;
    edited.name = "Act II";
    edited.frame = 5;  // timeline markers can be moved
    ASSERT_TRUE(session->updateMarker(edited).ok());
    EXPECT_EQ(session->timeline().markers.front().name, "Act II");  // re-sorted to the front
    ASSERT_TRUE(session->removeMarker(cm.value()).ok());
    EXPECT_EQ(session->markers().size(), 2u);
    session->undo();
    EXPECT_EQ(session->markers().size(), 3u);
    EXPECT_FALSE(session->removeMarker("nope").ok());
}

TEST_F(SessionTest, CopyPasteKeepsLinksAndUsesTargets) {
    ASSERT_TRUE(session->appendMedia(a).ok());  // a on V1/A1 [0,50)
    ASSERT_TRUE(session->addClipMarker(track(0).clips[0].id, 5, "m").ok());
    ASSERT_TRUE(session->copyClips({track(0).clips[0].id}).ok());
    EXPECT_EQ(session->clipboard().items.size(), 2u);  // linked audio came along
    EXPECT_EQ(session->clipboard().span, 50);

    // Paste on V2 (retargeted) + A1 at frame 60.
    ASSERT_TRUE(session->setTrackTargets(track(1).id, track(2).id).ok());
    auto pasted = session->paste(60, ops::EditMode::Overwrite);
    ASSERT_TRUE(pasted.ok()) << pasted.error().toString();
    ASSERT_EQ(pasted.value().size(), 2u);
    const Timeline& tl = session->timeline();
    const Clip* v = tl.clip(pasted.value()[0]);
    const Clip* au = tl.clip(pasted.value()[1]);
    EXPECT_EQ(tl.trackOfClip(v->id)->id, track(1).id);
    EXPECT_EQ(v->start, 60);
    EXPECT_EQ(v->linkId, au->linkId);
    EXPECT_NE(v->linkId, track(0).clips[0].linkId);  // a new link group
    EXPECT_NE(v->markers.at(0).id, track(0).clips[0].markers.at(0).id);
    EXPECT_TRUE(tl.validate().ok());

    // Pasting with the audio target disabled places only the video, unlinked.
    ASSERT_TRUE(session->setTrackTargets(track(1).id, "").ok());
    auto videoOnly = session->paste(120, ops::EditMode::Overwrite);
    ASSERT_TRUE(videoOnly.ok());
    ASSERT_EQ(videoOnly.value().size(), 1u);
    EXPECT_TRUE(tl.clip(videoOnly.value()[0])->linkId.empty());

    // Paste insert ripples every unlocked track; undo restores it in one step.
    ASSERT_TRUE(session->setTrackTargets(track(0).id, track(2).id).ok());
    ASSERT_TRUE(session->paste(0, ops::EditMode::Insert).ok());
    EXPECT_EQ(track(0).clips[1].start, 50);
    EXPECT_EQ(track(1).clips[0].start, 110);
    session->undo();
    EXPECT_EQ(track(0).clips[0].start, 0);
    EXPECT_EQ(track(1).clips[0].start, 60);
}

TEST_F(SessionTest, CutAndDuplicate) {
    ASSERT_TRUE(session->appendMedia(a).ok());
    ASSERT_TRUE(session->appendMedia(b).ok());  // b [50,90)
    const std::string bClip = track(0).clips[1].id;
    auto dup = session->duplicateClips({bClip});
    ASSERT_TRUE(dup.ok()) << dup.error().toString();
    ASSERT_EQ(dup.value().size(), 2u);
    EXPECT_EQ(session->timeline().clip(dup.value()[0])->start, 90);  // right after the original
    EXPECT_EQ(session->timeline().duration(), 130);
    EXPECT_TRUE(session->clipboard().empty());  // duplicate leaves the clipboard alone

    ASSERT_TRUE(session->cutClips({track(0).clips[0].id}).ok());
    EXPECT_EQ(track(0).clips.size(), 2u);  // a lifted (gap left)
    EXPECT_EQ(track(2).clips.size(), 2u);
    EXPECT_EQ(session->clipboard().items.size(), 2u);
    session->undo();  // one step restores both
    EXPECT_EQ(track(0).clips.size(), 3u);
    EXPECT_EQ(track(2).clips.size(), 3u);
}

TEST_F(SessionTest, PasteRejectsImpossibleDestinations) {
    EXPECT_FALSE(session->paste(0, ops::EditMode::Overwrite).ok());  // empty clipboard
    ASSERT_TRUE(session->appendMedia(a).ok());
    const std::string v1Clip = track(0).clips[0].id;
    ASSERT_TRUE(session->moveClip(v1Clip, track(1).id, 0).ok());  // now on V2 (+A1)
    ASSERT_TRUE(session->placeMedia(b, 0, ops::EditMode::Overwrite, track(0).id, track(3).id).ok());  // V1 + A2
    // Copy clips spanning V1..V2: pasting with V2 targeted needs a V3.
    ASSERT_TRUE(session->copyClips({track(0).clips[0].id, track(1).clips[0].id}).ok());
    ASSERT_TRUE(session->setTrackTargets(track(1).id, track(2).id).ok());
    auto r = session->paste(100, ops::EditMode::Overwrite);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::OutOfRange);
    ASSERT_TRUE(session->setTrackTargets("", "").ok());
    EXPECT_FALSE(session->paste(100, ops::EditMode::Overwrite).ok());
}

