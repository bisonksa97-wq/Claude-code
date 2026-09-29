#include <gtest/gtest.h>

#include "core/AtomicFile.h"
#include "core/Log.h"
#include "core/Rational.h"
#include "core/Result.h"
#include "core/Timecode.h"
#include "support/TestSupport.h"

using namespace up;

TEST(Rational, NormalizesAndParses) {
    EXPECT_EQ(Rational(50, 2), Rational(25, 1));
    EXPECT_EQ(Rational(3, -6), Rational(-1, 2));
    EXPECT_EQ(Rational::parse("30000/1001"), Rational(30000, 1001));
    EXPECT_EQ(Rational::parse("25"), Rational(25, 1));
    EXPECT_FALSE(Rational::parse("abc").valid());
    EXPECT_FALSE(Rational::parse("25/0").valid());
    EXPECT_FALSE(Rational::parse("25x").valid());
}

TEST(Rational, FrameSecondConversionsAreExactOnBoundaries) {
    const FrameRate ntsc{30000, 1001};
    for (FrameIndex f : {0, 1, 29, 30, 1799, 1800, 107892}) {
        EXPECT_EQ(secondsToFrames(framesToSeconds(f, ntsc), ntsc), f) << f;
    }
    EXPECT_EQ(secondsToFrames(1.0, FrameRate{25, 1}), 25);
}

TEST(Rational, FrameToSampleNeverDrifts) {
    const FrameRate ntsc{30000, 1001};
    // 30000/1001 fps at 48 kHz = 1601.6 samples/frame; after 5 frames exactly 8008 samples.
    EXPECT_EQ(frameToSample(5, ntsc, 48000), 8008);
    EXPECT_EQ(frameToSample(30000, ntsc, 48000), 48048000);
    EXPECT_EQ(frameToSample(25, FrameRate{25, 1}, 48000), 48000);
}

TEST(Timecode, NonDropFrame) {
    const FrameRate r{25, 1};
    EXPECT_EQ(formatTimecode(0, r), "00:00:00:00");
    EXPECT_EQ(formatTimecode(25 * 3600 + 25 * 61 + 3, r), "01:01:01:03");
    EXPECT_EQ(parseTimecode("01:01:01:03", r).value(), 25 * 3600 + 25 * 61 + 3);
    EXPECT_EQ(parseTimecode("42", r).value(), 42);
    EXPECT_FALSE(parseTimecode("00:00:00:25", r).has_value());
    EXPECT_FALSE(parseTimecode("garbage", r).has_value());
}

TEST(Timecode, DropFrameSkipsLabelsAtMinuteBoundaries) {
    const FrameRate r{30000, 1001};
    EXPECT_EQ(formatTimecode(1799, r, true), "00:00:59;29");
    EXPECT_EQ(formatTimecode(1800, r, true), "00:01:00;02");
    EXPECT_EQ(formatTimecode(17982, r, true), "00:10:00;00");
    for (FrameIndex f : {0, 1799, 1800, 17982, 107892, 123456}) {
        EXPECT_EQ(parseTimecode(formatTimecode(f, r, true), r).value(), f) << f;
    }
}

TEST(Error, IsHumanReadable) {
    const Error e = makeError(ErrorCode::DecodeError, "codec", "Unable to decode HEVC frame 1842.", "Re-import the file.",
                              "hw decoder returned invalid frame");
    EXPECT_EQ(e.id(), "UP-CODEC-DECODE_ERROR");
    const std::string text = e.toString();
    EXPECT_NE(text.find("Unable to decode HEVC frame 1842."), std::string::npos);
    EXPECT_NE(text.find("Suggested action: Re-import the file."), std::string::npos);
    EXPECT_NE(text.find("Technical details:"), std::string::npos);
}

TEST(Log, RespectsPerSubsystemLevels) {
    std::vector<std::string> seen;
    log::setSink([&](log::Level, std::string_view sub, std::string_view msg) {
        seen.push_back(std::string(sub) + ":" + std::string(msg));
    });
    log::setDefaultLevel(log::Level::Warning);
    log::setLevel("codec", log::Level::Debug);
    UP_LOG_DEBUG("codec", "decoded " << 3);
    UP_LOG_DEBUG("media", "hidden");
    UP_LOG_WARN("media", "shown");
    log::setSink(nullptr);
    log::setLevel("codec", log::Level::Warning);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], "codec:decoded 3");
    EXPECT_EQ(seen[1], "media:shown");
}

TEST(AtomicFile, WritesAndKeepsBackup) {
    test::TempDir dir;
    const auto file = dir / "a.txt";
    ASSERT_TRUE(writeFileAtomically(file, "one").ok());
    ASSERT_TRUE(writeFileAtomically(file, "two").ok());
    EXPECT_EQ(readFile(file).value(), "two");
    EXPECT_EQ(readFile(dir / "a.txt.bak").value(), "one");
    EXPECT_FALSE(std::filesystem::exists(dir / "a.txt.tmp"));
}

TEST(AtomicFile, FailsCleanlyForMissingFolder) {
    test::TempDir dir;
    const Status s = writeFileAtomically(dir / "missing" / "a.txt", "x");
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, ErrorCode::IoError);
}
