#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "app/EditorSession.h"
#include "playback/PlaybackEngine.h"
#include "render/AudioMixer.h"
#include "support/TestSupport.h"

using namespace up;
using namespace up::playback;
using namespace std::chrono_literals;

namespace {

// Audio device double: the test decides when and how much the "device" consumes.
class FakeAudioOutput final : public AudioOutput {
public:
    Status start(int sampleRate, int channels, Pull pull) override {
        if (failStart) return makeError(ErrorCode::NotFound, "audio", "No audio device.");
        rate = sampleRate;
        ch = channels;
        pull_ = std::move(pull);
        started = true;
        return Status::success();
    }
    void stop() override {
        started = false;
        pull_ = nullptr;
    }
    int64_t playedFrames() const override { return played; }

    // Consumes `frames` frames as a device would, appending them to `captured`.
    void pump(int64_t frames) {
        std::vector<float> buf(static_cast<std::size_t>(frames * ch));
        pull_(buf.data(), frames);
        captured.insert(captured.end(), buf.begin(), buf.end());
        played += frames;
    }

    bool failStart = false;
    bool started = false;
    int rate = 0;
    int ch = 0;
    std::atomic<int64_t> played{0};
    std::vector<float> captured;

private:
    Pull pull_;
};

class ManualClock final : public Clock {
public:
    void restart() override { seconds = 0; }
    double elapsedSeconds() const override { return seconds; }
    std::atomic<double> seconds{0};
};

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

class PlaybackTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dir = new test::TempDir();
        ASSERT_TRUE(media::generateSyntheticMedia(*dir / "red.mp4", test::solid(220, 20, 20, 25, 440)).ok());
        ASSERT_TRUE(media::generateSyntheticMedia(*dir / "blue.mp4", test::solid(20, 20, 220, 25, 880)).ok());
    }
    static void TearDownTestSuite() {
        delete dir;
        dir = nullptr;
    }
    void SetUp() override {
        session = EditorSession::createNew("Play", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
        const auto ids = session->importMedia({*dir / "red.mp4", *dir / "blue.mp4"}).importedIds;
        ASSERT_EQ(ids.size(), 2u);
        ASSERT_TRUE(session->appendMedia(ids[0]).ok());
        ASSERT_TRUE(session->appendMedia(ids[1]).ok());  // red 0..25, blue 25..50
    }

    static test::TempDir* dir;
    std::unique_ptr<EditorSession> session;
};

test::TempDir* PlaybackTest::dir = nullptr;

}  // namespace

TEST(SampleFifo, WrapsAroundAndBounds) {
    SampleFifo fifo(2, 4);
    const float in[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    EXPECT_EQ(fifo.push(in, 3), 3);
    float out[8] = {};
    EXPECT_EQ(fifo.pop(out, 2), 2);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[2], 2);
    EXPECT_EQ(fifo.push(in + 6, 2), 2);  // wraps
    EXPECT_EQ(fifo.push(in, 5), 1);      // only one frame of space left
    EXPECT_EQ(fifo.size(), 4);
    EXPECT_EQ(fifo.pop(out, 10), 4);
    EXPECT_EQ(out[0], 3);
    EXPECT_EQ(out[2], 4);
    EXPECT_EQ(out[4], 5);
    EXPECT_EQ(out[6], 1);
}

TEST_F(PlaybackTest, AudioMatchesOfflineMixAndDrivesTheClock) {
    auto device = std::make_shared<FakeAudioOutput>();
    PlaybackEngine engine(device);
    ASSERT_TRUE(engine.start(session->project(), session->timeline().id, 10, 80, 60).ok());
    ASSERT_TRUE(engine.usingAudioClock());
    EXPECT_EQ(engine.position(), 10);

    // Consume 1.2 s in device-sized chunks, waiting for the mixer like a real device would.
    for (int i = 0; i < 56; ++i) {
        ASSERT_TRUE(waitFor([&] { return engine.bufferedAudioFrames() >= 1024; }));
        device->pump(1024);
    }
    EXPECT_EQ(engine.stats().audioUnderruns, 0);
    // 57344 samples played = 1.1947 s = 29 frames after frame 10.
    EXPECT_EQ(engine.position(), 10 + 29);

    // The played audio is exactly the offline mix of the same range (continuous, no gaps).
    render::AudioMixer mixer(render::resolverFor(session->project()));
    std::vector<float> expected;
    ASSERT_TRUE(mixer.mix(session->timeline(), frameToSample(10, FrameRate{25, 1}, 48000), 56 * 1024, expected).ok());
    ASSERT_EQ(device->captured.size(), expected.size());
    double maxDiff = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(expected[i] - device->captured[i])));
    EXPECT_LT(maxDiff, 1e-6);
    engine.stop();
    EXPECT_FALSE(device->started);
}

TEST_F(PlaybackTest, VideoFollowsClockAndSkipsLateFrames) {
    auto clock = std::make_shared<ManualClock>();
    PlaybackEngine engine(nullptr, clock);
    ASSERT_TRUE(engine.start(session->project(), session->timeline().id, 0, 80, 60).ok());
    EXPECT_FALSE(engine.usingAudioClock());

    std::optional<DisplayFrame> frame;
    ASSERT_TRUE(waitFor([&] { return (frame = engine.frameForDisplay()).has_value(); }));
    EXPECT_EQ(frame->index, 0);
    EXPECT_EQ(frame->image.width, 80);
    EXPECT_GT(test::averageColor(frame->image).r, 180);
    EXPECT_FALSE(engine.frameForDisplay().has_value());  // nothing newer until the clock moves

    clock->seconds = 1.2;  // frame 30: blue clip
    ASSERT_TRUE(waitFor([&] {
        frame = engine.frameForDisplay();
        return frame && frame->index == 30;
    }));
    EXPECT_GT(test::averageColor(frame->image).b, 180);
    EXPECT_GT(engine.stats().droppedFrames, 20);  // frames 1..29 were never shown

    clock->seconds = 3.0;
    EXPECT_TRUE(engine.finished());
    engine.stop();
    EXPECT_FALSE(engine.isRunning());
}

TEST_F(PlaybackTest, FallsBackToWallClockWithoutAudioDevice) {
    auto device = std::make_shared<FakeAudioOutput>();
    device->failStart = true;
    auto clock = std::make_shared<ManualClock>();
    PlaybackEngine engine(device, clock);
    ASSERT_TRUE(engine.start(session->project(), session->timeline().id, 5, 80, 60).ok());
    EXPECT_FALSE(engine.usingAudioClock());
    clock->seconds = 0.4;
    EXPECT_EQ(engine.position(), 15);
}

TEST_F(PlaybackTest, PlaysASnapshotSoEditsDoNotRace) {
    auto clock = std::make_shared<ManualClock>();
    PlaybackEngine engine(nullptr, clock);
    ASSERT_TRUE(engine.start(session->project(), session->timeline().id, 0, 80, 60).ok());
    // Editing the live model while playing must not affect (or crash) the running engine.
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(session->razorAt(2 + i).ok());
        clock->seconds = i * 0.04;
        (void)engine.frameForDisplay();
    }
    ASSERT_TRUE(engine.lastError().ok());
    EXPECT_EQ(engine.endFrame(), 50);
}

TEST_F(PlaybackTest, RejectsStartOutsideTimeline) {
    PlaybackEngine engine;
    EXPECT_EQ(engine.start(session->project(), session->timeline().id, 50, 80, 60).error().code, ErrorCode::OutOfRange);
    EXPECT_EQ(engine.start(session->project(), "nope", 0, 80, 60).error().code, ErrorCode::NotFound);
}
