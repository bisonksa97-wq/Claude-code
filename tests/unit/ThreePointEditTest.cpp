#include <gtest/gtest.h>

#include "timeline/ThreePointEdit.h"

using namespace up;
using namespace up::ops;

namespace {

struct Case {
    const char* name;
    std::optional<FrameIndex> sIn, sOut, rIn, rOut;
    FrameIndex playhead;
    FrameIndex length;
    // expected
    FrameIndex srcIn, recIn, duration;
};

}  // namespace

// Every combination of the four marks, against a 100-frame source with the playhead at 40.
TEST(ThreePointEdit, AllMarkCombinations) {
    const std::optional<FrameIndex> none;
    const Case cases[] = {
        {"no marks: whole source at playhead", none, none, none, none, 40, 100, 0, 40, 100},
        {"source in: rest of source", 10, none, none, none, 40, 100, 10, 40, 90},
        {"source out: from start to out", none, 30, none, none, 40, 100, 0, 40, 30},
        {"source in+out at playhead", 10, 30, none, none, 40, 100, 10, 40, 20},
        {"record in only", none, none, 5, none, 40, 100, 0, 5, 100},
        {"record out only: backtimed", none, none, none, 250, 40, 100, 0, 150, 100},
        {"record in+out: fills range from source start", none, none, 5, 25, 40, 100, 0, 5, 20},
        {"source in + record in", 10, none, 5, none, 40, 100, 10, 5, 90},
        {"source in + record out: backtimed record", 10, none, none, 200, 40, 100, 10, 110, 90},
        {"source in + record range", 10, none, 5, 25, 40, 100, 10, 5, 20},
        {"source out + record in", none, 30, 5, none, 40, 100, 0, 5, 30},
        {"source out + record out", none, 30, none, 60, 40, 100, 0, 30, 30},
        {"source out + record range: backtimed source", none, 30, 5, 25, 40, 100, 10, 5, 20},
        {"source range + record in (classic)", 10, 30, 5, none, 40, 100, 10, 5, 20},
        {"source range + record out (backtimed)", 10, 30, none, 60, 40, 100, 10, 40, 20},
        {"four points: timeline range wins, source in kept", 10, 30, 5, 15, 40, 100, 10, 5, 10},
    };
    for (const auto& c : cases) {
        ThreePointInput in;
        in.sourceIn = c.sIn;
        in.sourceOut = c.sOut;
        in.recordIn = c.rIn;
        in.recordOut = c.rOut;
        in.playhead = c.playhead;
        in.sourceLength = c.length;
        auto r = resolveThreePointEdit(in);
        ASSERT_TRUE(r.ok()) << c.name << ": " << r.error().message;
        EXPECT_EQ(r.value().sourceIn, c.srcIn) << c.name;
        EXPECT_EQ(r.value().recordIn, c.recIn) << c.name;
        EXPECT_EQ(r.value().duration, c.duration) << c.name;
        EXPECT_EQ(r.value().recordOut(), c.recIn + c.duration) << c.name;
    }
}

TEST(ThreePointEdit, UnboundedSourcesUseTheDefaultDuration) {
    ThreePointInput in;
    in.playhead = 7;
    in.defaultDuration = 125;
    auto r = resolveThreePointEdit(in);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().duration, 125);
    in.recordIn = 0;
    in.recordOut = 1000;  // a still can fill any range
    EXPECT_EQ(resolveThreePointEdit(in).value().duration, 1000);
}

TEST(ThreePointEdit, RejectsImpossibleEdits) {
    ThreePointInput in;
    in.sourceLength = 100;
    in.sourceIn = 90;
    in.recordIn = 0;
    in.recordOut = 20;  // needs 20 frames, only 10 after the in mark
    EXPECT_EQ(resolveThreePointEdit(in).error().code, ErrorCode::OutOfRange);

    ThreePointInput back;
    back.sourceLength = 100;
    back.sourceOut = 5;
    back.recordIn = 0;
    back.recordOut = 20;  // backtiming from source out 5 would start at -15
    EXPECT_EQ(resolveThreePointEdit(back).error().code, ErrorCode::OutOfRange);

    ThreePointInput early;
    early.sourceLength = 100;
    early.recordOut = 30;  // 100 frames ending at 30 would start at -70
    EXPECT_EQ(resolveThreePointEdit(early).error().code, ErrorCode::OutOfRange);

    ThreePointInput empty;
    empty.sourceLength = 100;
    empty.sourceIn = 100;
    EXPECT_EQ(resolveThreePointEdit(empty).error().code, ErrorCode::InvalidArgument);
}
