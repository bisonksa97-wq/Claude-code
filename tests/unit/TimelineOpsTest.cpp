#include <gtest/gtest.h>

#include <random>
#include <sstream>

#include "timeline/EditOperations.h"

using namespace up;
using namespace up::ops;

namespace {

// Compact description of a track: "[start,end)@sourceIn" per clip.
std::string describe(const Track& t) {
    std::ostringstream os;
    for (const auto& c : t.clips) os << "[" << c.start << "," << c.end() << ")@" << c.sourceIn << " ";
    return os.str();
}

std::string describe(const Timeline& tl) {
    std::ostringstream os;
    for (const auto& t : tl.tracks) os << t.name << ": " << describe(t) << "| ";
    return os.str();
}

Clip makeClip(FrameIndex start, FrameIndex duration, FrameIndex sourceIn = 0, FrameIndex sourceLength = 1000) {
    Clip c;
    c.mediaId = "m";
    c.name = "clip";
    c.start = start;
    c.duration = duration;
    c.sourceIn = sourceIn;
    c.sourceLength = sourceLength;
    return c;
}

class TimelineOps : public ::testing::Test {
protected:
    void SetUp() override {
        tl = Timeline::create("T", FrameRate{25, 1}, 1920, 1080, 48000, 2, 1);
        v1 = tl.tracks[0].id;
        v2 = tl.tracks[1].id;
        a1 = tl.tracks[2].id;
    }

    std::string put(FrameIndex start, FrameIndex duration, FrameIndex sourceIn = 0, FrameIndex length = 1000) {
        auto r = placeClip(tl, v1, makeClip(start, duration, sourceIn, length), EditMode::Overwrite);
        EXPECT_TRUE(r.ok());
        return r.ok() ? r.value() : std::string();
    }

    const Track& V1() const { return *tl.track(v1); }

    Timeline tl;
    std::string v1, v2, a1;
};

}  // namespace

TEST_F(TimelineOps, OverwriteSplitsAndReplaces) {
    put(0, 100, 0);
    put(40, 20, 500);
    EXPECT_EQ(describe(V1()), "[0,40)@0 [40,60)@500 [60,100)@60 ");
    EXPECT_TRUE(tl.validate().ok());
}

TEST_F(TimelineOps, InsertRipplesLaterClips) {
    put(0, 50, 0);
    put(50, 50, 100);
    auto r = placeClip(tl, v1, makeClip(25, 10, 700), EditMode::Insert);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(describe(V1()), "[0,25)@0 [25,35)@700 [35,60)@25 [60,110)@100 ");
}

TEST_F(TimelineOps, AppendPlacesAtTrackEnd) {
    put(0, 30);
    ASSERT_TRUE(appendClip(tl, v1, makeClip(999, 10, 5)).ok());
    EXPECT_EQ(describe(V1()), "[0,30)@0 [30,40)@5 ");
}

TEST_F(TimelineOps, RazorSplitsAtFrame) {
    const auto id = put(10, 40, 100);
    auto right = razor(tl, id, 30);
    ASSERT_TRUE(right.ok());
    EXPECT_EQ(describe(V1()), "[10,30)@100 [30,50)@120 ");
    EXPECT_EQ(tl.clip(right.value())->start, 30);
}

TEST_F(TimelineOps, RazorRejectsEdges) {
    const auto id = put(10, 40);
    EXPECT_EQ(razor(tl, id, 10).error().code, ErrorCode::OutOfRange);
    EXPECT_EQ(razor(tl, id, 50).error().code, ErrorCode::OutOfRange);
    EXPECT_EQ(describe(V1()), "[10,50)@0 ");
}

TEST_F(TimelineOps, LiftLeavesGapRippleDeleteClosesIt) {
    put(0, 10);
    const auto mid = put(10, 10, 50);
    put(20, 10, 90);
    Timeline copy = tl;
    ASSERT_TRUE(lift(tl, mid).ok());
    EXPECT_EQ(describe(V1()), "[0,10)@0 [20,30)@90 ");
    tl = copy;
    ASSERT_TRUE(rippleDelete(tl, mid).ok());
    EXPECT_EQ(describe(V1()), "[0,10)@0 [10,20)@90 ");
}

TEST_F(TimelineOps, NormalTrimOutIsBlockedByNextClip) {
    const auto a = put(0, 10);
    put(10, 10, 50);
    EXPECT_TRUE(trim(tl, a, Edge::Out, -3, TrimMode::Normal).ok());
    EXPECT_EQ(describe(V1()), "[0,7)@0 [10,20)@50 ");
    EXPECT_TRUE(trim(tl, a, Edge::Out, 3, TrimMode::Normal).ok());
    EXPECT_EQ(trim(tl, a, Edge::Out, 1, TrimMode::Normal).error().code, ErrorCode::OutOfRange);
}

TEST_F(TimelineOps, TrimRespectsSourceBounds) {
    const auto a = put(10, 10, 5, 20);  // media is 20 frames, using 5..15
    EXPECT_EQ(trim(tl, a, Edge::Out, 6, TrimMode::Normal).error().code, ErrorCode::OutOfRange);
    EXPECT_TRUE(trim(tl, a, Edge::Out, 5, TrimMode::Normal).ok());
    EXPECT_EQ(trim(tl, a, Edge::In, -6, TrimMode::Normal).error().code, ErrorCode::OutOfRange);
    EXPECT_TRUE(trim(tl, a, Edge::In, -5, TrimMode::Normal).ok());
    EXPECT_EQ(describe(V1()), "[5,25)@0 ");
    EXPECT_EQ(trim(tl, a, Edge::Out, -20, TrimMode::Normal).error().code, ErrorCode::OutOfRange);
}

TEST_F(TimelineOps, NormalTrimInMovesStartAndSource) {
    const auto a = put(10, 20, 100);
    ASSERT_TRUE(trim(tl, a, Edge::In, 5, TrimMode::Normal).ok());
    EXPECT_EQ(describe(V1()), "[15,30)@105 ");
}

TEST_F(TimelineOps, RippleTrimShiftsFollowingClips) {
    const auto a = put(0, 10);
    put(10, 10, 50);
    put(30, 5, 70);
    ASSERT_TRUE(trim(tl, a, Edge::Out, -4, TrimMode::Ripple).ok());
    EXPECT_EQ(describe(V1()), "[0,6)@0 [6,16)@50 [26,31)@70 ");
    ASSERT_TRUE(trim(tl, a, Edge::In, 2, TrimMode::Ripple).ok());
    EXPECT_EQ(describe(V1()), "[0,4)@2 [4,14)@50 [24,29)@70 ");
    ASSERT_TRUE(trim(tl, a, Edge::Out, 20, TrimMode::Ripple).ok());
    EXPECT_EQ(describe(V1()), "[0,24)@2 [24,34)@50 [44,49)@70 ");
}

TEST_F(TimelineOps, RollMovesEditPoint) {
    const auto a = put(0, 10, 0);
    put(10, 10, 50);
    ASSERT_TRUE(roll(tl, a, 3).ok());
    EXPECT_EQ(describe(V1()), "[0,13)@0 [13,20)@53 ");
    ASSERT_TRUE(roll(tl, a, -5).ok());
    EXPECT_EQ(describe(V1()), "[0,8)@0 [8,20)@48 ");
    EXPECT_EQ(roll(tl, a, 12).error().code, ErrorCode::OutOfRange);
}

TEST_F(TimelineOps, RollNeedsAdjacentClip) {
    const auto a = put(0, 10);
    put(15, 10);
    EXPECT_EQ(roll(tl, a, 1).error().code, ErrorCode::InvalidArgument);
}

TEST_F(TimelineOps, SlipChangesSourceOnly) {
    const auto a = put(10, 10, 5, 30);
    ASSERT_TRUE(slip(tl, a, 7).ok());
    EXPECT_EQ(describe(V1()), "[10,20)@12 ");
    EXPECT_FALSE(slip(tl, a, 9).ok());   // 21..31 exceeds 30
    EXPECT_FALSE(slip(tl, a, -13).ok());  // before frame 0
    EXPECT_EQ(describe(V1()), "[10,20)@12 ");
}

TEST_F(TimelineOps, SlideTrimsNeighbours) {
    put(0, 10, 0);
    const auto mid = put(10, 10, 100);
    put(20, 10, 200);
    ASSERT_TRUE(slide(tl, mid, 3).ok());
    EXPECT_EQ(describe(V1()), "[0,13)@0 [13,23)@100 [23,30)@203 ");
    ASSERT_TRUE(slide(tl, mid, -5).ok());
    EXPECT_EQ(describe(V1()), "[0,8)@0 [8,18)@100 [18,30)@198 ");
    EXPECT_FALSE(slide(tl, mid, -8).ok());  // left neighbour would vanish
}

TEST_F(TimelineOps, MoveClipOverwritesDestination) {
    put(0, 10);
    const auto b = put(20, 10, 500);
    ASSERT_TRUE(placeClip(tl, v2, makeClip(0, 30), EditMode::Overwrite).ok());
    ASSERT_TRUE(moveClip(tl, b, v2, 5).ok());
    EXPECT_EQ(describe(V1()), "[0,10)@0 ");
    EXPECT_EQ(describe(*tl.track(v2)), "[0,5)@0 [5,15)@500 [15,30)@15 ");
    EXPECT_EQ(moveClip(tl, b, a1, 0).error().code, ErrorCode::InvalidArgument);
}

TEST_F(TimelineOps, LockedTracksRejectEdits) {
    const auto a = put(0, 10);
    tl.track(v1)->locked = true;
    EXPECT_EQ(razor(tl, a, 5).error().code, ErrorCode::Locked);
    EXPECT_EQ(lift(tl, a).error().code, ErrorCode::Locked);
    EXPECT_EQ(placeClip(tl, v1, makeClip(20, 5), EditMode::Overwrite).error().code, ErrorCode::Locked);
    EXPECT_EQ(describe(V1()), "[0,10)@0 ");
}

TEST_F(TimelineOps, InsertGapSplitsStraddlingClip) {
    put(0, 20, 0);
    ASSERT_TRUE(insertGap(tl, v1, 5, 10).ok());
    EXPECT_EQ(describe(V1()), "[0,5)@0 [15,30)@5 ");
}

TEST_F(TimelineOps, DurationIsLastClipEnd) {
    EXPECT_EQ(tl.duration(), 0);
    put(10, 20);
    ASSERT_TRUE(placeClip(tl, a1, makeClip(0, 45), EditMode::Overwrite).ok());
    EXPECT_EQ(tl.duration(), 45);
}

// Randomised stress test: any sequence of operations must keep the timeline valid,
// and a failed operation must leave it exactly unchanged.
TEST_F(TimelineOps, RandomOperationsPreserveInvariants) {
    std::mt19937 rng(1234);
    auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const std::vector<std::string> tracks{v1, v2};
    for (int step = 0; step < 3000; ++step) {
        const std::string before = describe(tl);
        std::vector<std::string> clipIds;
        for (const auto& t : tl.tracks)
            for (const auto& c : t.clips) clipIds.push_back(c.id);
        const std::string clip = clipIds.empty() ? "none" : clipIds[static_cast<std::size_t>(pick(0, static_cast<int>(clipIds.size()) - 1))];
        const std::string track = tracks[static_cast<std::size_t>(pick(0, 1))];
        bool ok = false;
        switch (pick(0, 10)) {
            case 0:
            case 1: ok = placeClip(tl, track, makeClip(pick(0, 300), pick(1, 60), pick(0, 100), 200),
                                   pick(0, 1) ? EditMode::Insert : EditMode::Overwrite).ok(); break;
            case 2: ok = razor(tl, clip, pick(0, 400)).ok(); break;
            case 3: ok = lift(tl, clip).ok(); break;
            case 4: ok = rippleDelete(tl, clip).ok(); break;
            case 5: ok = trim(tl, clip, pick(0, 1) ? Edge::In : Edge::Out, pick(-20, 20),
                              pick(0, 1) ? TrimMode::Ripple : TrimMode::Normal).ok(); break;
            case 6: ok = roll(tl, clip, pick(-10, 10)).ok(); break;
            case 7: ok = slip(tl, clip, pick(-30, 30)).ok(); break;
            case 8: ok = slide(tl, clip, pick(-10, 10)).ok(); break;
            case 9: ok = moveClip(tl, clip, track, pick(0, 300)).ok(); break;
            case 10: ok = insertGap(tl, track, pick(0, 300), pick(1, 20)).ok(); break;
        }
        ASSERT_TRUE(tl.validate().ok()) << "step " << step << ": " << describe(tl);
        if (!ok) {
            ASSERT_EQ(describe(tl), before) << "failed op modified the timeline at step " << step;
        }
    }
}
