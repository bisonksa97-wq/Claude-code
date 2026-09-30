#include <gtest/gtest.h>

#include <cmath>

#include "timeline/EditOperations.h"
#include "timeline/Transitions.h"

using namespace up;
using namespace up::transitions;

namespace {

// A [0,50) using source 0..50 of 100 (50 frames of tail handle);
// B [50,100) using source 20..70 of 100 (20 frames of head handle).
Track makeTrack() {
    Track t;
    t.id = "t";
    Clip a;
    a.id = "a";
    a.start = 0;
    a.duration = 50;
    a.sourceIn = 0;
    a.sourceLength = 100;
    Clip b = a;
    b.id = "b";
    b.start = 50;
    b.sourceIn = 20;
    t.clips = {a, b};
    return t;
}

Transition make(FrameIndex d, TransitionAlignment align = TransitionAlignment::Center,
                TransitionKind kind = TransitionKind::Dissolve) {
    return Transition{kind, d, align};
}

}  // namespace

TEST(Transitions, EditPointAlignment) {
    Track t = makeTrack();
    t.clips[1].transitionIn = make(20);
    auto r = regions(t);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].start, 40);
    EXPECT_EQ(r[0].end, 60);
    EXPECT_EQ(r[0].outgoing, &t.clips[0]);
    EXPECT_EQ(r[0].incoming, &t.clips[1]);

    t.clips[1].transitionIn = make(20, TransitionAlignment::StartAtCut);
    r = regions(t);
    EXPECT_EQ(r[0].start, 50);
    EXPECT_EQ(r[0].end, 70);
    t.clips[1].transitionIn = make(30, TransitionAlignment::EndAtCut);
    r = regions(t);
    EXPECT_EQ(r[0].start, 30);  // clamped to B's 20-frame head handle
    EXPECT_EQ(r[0].end, 50);
    EXPECT_EQ(r[0].requested, 30);
}

TEST(Transitions, HandlesAndClipLengthsClamp) {
    Track t = makeTrack();
    t.clips[1].sourceIn = 4;  // only 4 frames before B's in point
    t.clips[1].transitionIn = make(20);
    auto r = regions(t);
    EXPECT_EQ(r[0].start, 46);
    EXPECT_EQ(r[0].end, 60);
    EXPECT_EQ(maxDuration(t, t.clips[1], true, TransitionAlignment::Center), 9);
    EXPECT_EQ(maxDuration(t, t.clips[1], true, TransitionAlignment::StartAtCut), 50);
    EXPECT_EQ(maxDuration(t, t.clips[1], true, TransitionAlignment::EndAtCut), 4);
    // No handle at all on either side: the transition disappears instead of breaking.
    t.clips[1].sourceIn = 0;
    t.clips[0].sourceLength = 50;
    EXPECT_TRUE(regions(t).empty());
}

TEST(Transitions, FadesAtFreeEnds) {
    Track t = makeTrack();
    t.clips[1].start = 60;  // not adjacent any more
    t.clips[1].transitionIn = make(10);
    t.clips[0].transitionIn = make(5);
    t.clips[1].transitionOut = make(8);
    auto r = regions(t);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0].start, 0);  // fade in of A
    EXPECT_EQ(r[0].end, 5);
    EXPECT_EQ(r[0].outgoing, nullptr);
    EXPECT_EQ(r[1].start, 60);  // fade in of B
    EXPECT_EQ(r[1].end, 70);
    EXPECT_EQ(r[2].start, 102);  // fade out of B
    EXPECT_EQ(r[2].end, 110);
    EXPECT_EQ(r[2].incoming, nullptr);

    // A tail fade gives way to an edit-point transition owned by the next clip.
    Track u = makeTrack();
    u.clips[0].transitionOut = make(10);
    u.clips[1].transitionIn = make(10);
    EXPECT_EQ(regions(u).size(), 1u);
}

TEST(Transitions, EvaluateAndWeights) {
    Track t = makeTrack();
    t.clips[1].transitionIn = make(20);
    const auto r = regions(t);
    auto f = evaluate(t, r, 45);
    ASSERT_NE(f.region, nullptr);
    EXPECT_DOUBLE_EQ(f.progress, (45 - 40 + 0.5) / 20.0);
    f = evaluate(t, r, 30);
    EXPECT_EQ(f.region, nullptr);
    EXPECT_EQ(f.clip, &t.clips[0]);

    auto w = videoWeights(TransitionKind::Dissolve, 0.25);
    EXPECT_DOUBLE_EQ(w.outgoing, 0.75);
    EXPECT_DOUBLE_EQ(w.incoming, 0.25);
    w = videoWeights(TransitionKind::Dip, 0.25);
    EXPECT_DOUBLE_EQ(w.outgoing, 0.5);
    EXPECT_DOUBLE_EQ(w.below, 0.5);
    w = videoWeights(TransitionKind::Dip, 0.75);
    EXPECT_DOUBLE_EQ(w.below, 0.5);
    EXPECT_DOUBLE_EQ(w.incoming, 0.5);
    for (double p : {0.0, 0.3, 0.5, 0.9, 1.0}) {
        const auto [o, i] = audioGains(TransitionKind::Dissolve, p);
        EXPECT_NEAR(o * o + i * i, 1.0, 1e-12) << p;  // constant power
    }
    EXPECT_DOUBLE_EQ(audioGains(TransitionKind::Dip, 0.5).first, 0.0);
}

TEST(Transitions, AudioRangesAndEnvelope) {
    Track t = makeTrack();
    t.clips[1].transitionIn = make(20);
    const auto r = regions(t);
    EXPECT_EQ(audibleRange(r, t.clips[0]), (std::pair<FrameIndex, FrameIndex>{0, 60}));   // into A's tail handle
    EXPECT_EQ(audibleRange(r, t.clips[1]), (std::pair<FrameIndex, FrameIndex>{40, 100})); // from B's head handle
    EXPECT_DOUBLE_EQ(audioEnvelope(r, t.clips[0], 20.0), 1.0);
    EXPECT_NEAR(audioEnvelope(r, t.clips[0], 50.0), std::cos(0.5 * 1.5707963267948966), 1e-9);
    EXPECT_NEAR(audioEnvelope(r, t.clips[1], 50.0), std::sin(0.5 * 1.5707963267948966), 1e-9);
    EXPECT_DOUBLE_EQ(audioEnvelope(r, t.clips[0], 70.0), 0.0);
}

TEST(Transitions, RazorKeepsHeadAndTailTransitionsOnTheRightPieces) {
    Timeline tl = Timeline::create("T", FrameRate{25, 1}, 1920, 1080, 48000, 1, 0);
    Clip c;
    c.mediaId = "m";
    c.start = 0;
    c.duration = 100;
    c.sourceLength = 200;
    c.transitionIn = make(10);
    c.transitionOut = make(12);
    auto id = ops::placeClip(tl, tl.tracks[0].id, c, ops::EditMode::Overwrite);
    ASSERT_TRUE(id.ok());
    auto right = ops::razor(tl, id.value(), 50);
    ASSERT_TRUE(right.ok());
    EXPECT_TRUE(tl.clip(id.value())->transitionIn.has_value());
    EXPECT_FALSE(tl.clip(id.value())->transitionOut.has_value());
    EXPECT_FALSE(tl.clip(right.value())->transitionIn.has_value());
    EXPECT_EQ(tl.clip(right.value())->transitionOut->duration, 12);
}
