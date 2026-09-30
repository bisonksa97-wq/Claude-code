// Delivery: presets, high-bit-depth decode/encode through the whole pipeline, HDR
// metadata, image sequences and the render queue.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <set>
#include <thread>

#include "app/EditorSession.h"
#include "codec/AudioDecoder.h"
#include "codec/MediaProbe.h"
#include "codec/MediaWriter.h"
#include "codec/VideoDecoder.h"
#include "render/ExportJob.h"
#include "render/FrameCompositor.h"
#include "render/RenderQueue.h"
#include "support/TestSupport.h"

using namespace up;
using namespace up::render;

namespace {

const ExportPreset& preset(const std::string& id) { return *findPreset(builtInPresets(), id); }

// 10-bit lossless source whose picture is a narrow horizontal ramp (0.40..0.44):
// ~41 levels at 10 bits, ~11 at 8 bits.
void makeTenBitRamp(const std::filesystem::path& path, int width, int height, int frames) {
    EncodeSettings s;
    s.width = width;
    s.height = height;
    s.audio = false;
    s.videoCodec = "ffv1";
    s.pixelFormat = "gbrp10le";
    auto writer = MediaWriter::open(path, s);
    ASSERT_TRUE(writer.ok()) << writer.error().toString();
    VideoFrame16 f(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto v = static_cast<uint16_t>(std::lround((0.40 + 0.04 * x / (width - 1)) * 65535.0));
            uint16_t* px = f.row(y) + x * 4;
            px[0] = px[1] = px[2] = v;
            px[3] = 65535;
        }
    for (int i = 0; i < frames; ++i) ASSERT_TRUE(writer.value()->writeVideo(f).ok());
    ASSERT_TRUE(writer.value()->finish().ok());
}

std::size_t levelsIn(const std::filesystem::path& file, int bits) {
    auto decoder = VideoDecoder::open(file);
    EXPECT_TRUE(decoder.ok());
    const VideoFrame16 f = decoder.value()->frameAt16(0.1).value();
    std::set<int> levels;
    for (int x = 0; x < f.width; ++x) levels.insert(f.row(f.height / 2)[x * 4] >> (16 - bits));
    return levels.size();
}

std::unique_ptr<EditorSession> sessionWith(const std::filesystem::path& media, int width, int height) {
    auto session = EditorSession::createNew("Delivery", SequenceSettings{FrameRate{25, 1}, width, height, 48000});
    const auto ids = session->importMedia({media}).importedIds;
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_TRUE(session->appendMedia(ids[0]).ok());
    return session;
}

}  // namespace

TEST(Delivery, TenBitSourcesStayTenBitThroughTheTimeline) {
    if (!encoderAvailable("ffv1")) GTEST_SKIP() << "ffv1 not available";
    test::TempDir dir;
    makeTenBitRamp(dir / "ramp10.mkv", 512, 64, 10);
    auto session = sessionWith(dir / "ramp10.mkv", 512, 64);
    EXPECT_EQ(session->project().media[0].info.bitDepth, 10);

    ExportOptions deep;
    deep.output = dir / "archive.mkv";
    deep.preset = preset("ffv1-archive");
    ASSERT_TRUE(ExportJob(session->project(), session->timeline().id, deep).run().ok());
    EXPECT_EQ(probeMedia(deep.output).value().bitDepth, 10);
    EXPECT_GE(levelsIn(deep.output, 10), 40u);  // decoded at 16 bits, graded in float, written at 10 bits

    // The same timeline through an 8-bit preset keeps only a quarter of the levels.
    ExportOptions web;
    web.output = dir / "web.mp4";
    web.preset = preset("h264-web");
    ASSERT_TRUE(ExportJob(session->project(), session->timeline().id, web).run().ok());
    EXPECT_LE(levelsIn(web.output, 8), 14u);
}

TEST(Delivery, HevcHdrExportCarriesMetadata) {
    if (!encoderAvailable("libx265")) GTEST_SKIP() << "libx265 not available";
    test::TempDir dir;
    test::makeMedia(dir / "grey.mp4", test::solid(128, 128, 128, 10));
    auto session = sessionWith(dir / "grey.mp4", 128, 64);
    ASSERT_TRUE(session->setOutputColorSpace(ColorSpace{Primaries::Rec2020, Transfer::PQ}).ok());
    ExportOptions options;
    options.output = dir / "hdr.mp4";
    options.preset = preset("hevc-10bit");
    options.maxCll = 400;
    options.maxFall = 120;
    ASSERT_TRUE(ExportJob(session->project(), session->timeline().id, options).run().ok());
    const MediaInfo info = probeMedia(options.output).value();
    EXPECT_EQ(info.videoCodec, "hevc");
    EXPECT_EQ(info.bitDepth, 10);
    EXPECT_EQ(info.colorPrimaries, "bt2020");
    EXPECT_EQ(info.colorTransfer, "smpte2084");
    EXPECT_NEAR(info.masteringMaxLuminance, 1000, 1e-6);
    EXPECT_EQ(info.maxCll, 400);
    EXPECT_EQ(info.maxFall, 120);
    // An SDR output writes no HDR metadata.
    ASSERT_TRUE(session->setOutputColorSpace(std::nullopt).ok());
    options.output = dir / "sdr.mp4";
    ASSERT_TRUE(ExportJob(session->project(), session->timeline().id, options).run().ok());
    EXPECT_EQ(probeMedia(options.output).value().masteringMaxLuminance, 0.0);
}

TEST(Delivery, ProResWavAndImageSequencePresets) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(200, 40, 40, 12));
    auto session = sessionWith(dir / "red.mp4", 64, 32);
    const std::string tl = session->timeline().id;
    if (encoderAvailable("prores_ks")) {
        ExportOptions master;
        master.output = dir / "master.mov";
        master.preset = preset("prores-422hq");
        ASSERT_TRUE(ExportJob(session->project(), tl, master).run().ok());
        const MediaInfo info = probeMedia(master.output).value();
        EXPECT_EQ(info.videoCodec, "prores");
        EXPECT_EQ(info.pixelFormat, "yuv422p10le");
        EXPECT_EQ(info.audioCodec, "pcm_s24le");
    }
    ExportOptions wav;
    wav.output = dir / "mix.wav";
    wav.preset = preset("wav-24");
    ASSERT_TRUE(ExportJob(session->project(), tl, wav).run().ok());
    const MediaInfo audio = probeMedia(wav.output).value();
    EXPECT_FALSE(audio.hasVideo);
    EXPECT_EQ(audio.audioCodec, "pcm_s24le");
    EXPECT_NEAR(audio.durationSeconds, 12 / 25.0, 0.03);

    ExportOptions seq;
    seq.output = dir / "shots";
    seq.preset = preset("png-16");
    ASSERT_TRUE(ExportJob(session->project(), tl, seq).run().ok());
    EXPECT_TRUE(std::filesystem::exists(dir / "shots" / "shots_000001.png"));
    EXPECT_TRUE(std::filesystem::exists(dir / "shots" / "shots_000012.png"));
    EXPECT_FALSE(std::filesystem::exists(dir / "shots" / "shots_000013.png"));
    EXPECT_FALSE(std::filesystem::exists(dir / "shots.partial"));
    const VideoFrame still = VideoDecoder::open(dir / "shots" / "shots_000005.png").value()->frameAt(0).value();
    EXPECT_NEAR(still.row(16)[32 * 4], 200, 3);  // centre (the 4:3 source is pillarboxed in 2:1)
    EXPECT_EQ(still.row(16)[0], 0);
    // Writing into an existing folder is refused rather than mixing sequences.
    auto again = ExportJob(session->project(), tl, seq).run();
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.error().code, ErrorCode::Conflict);
    // A preset whose encoder is missing fails before creating anything.
    ExportPreset missing = preset("h264-web");
    missing.videoCodec = "no_such_encoder";
    ExportOptions bad;
    bad.output = dir / "bad.mp4";
    bad.preset = missing;
    auto refused = ExportJob(session->project(), tl, bad).run();
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("no_such_encoder"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(dir / "bad.partial.mp4"));
}

TEST(Delivery, RenderQueueRunsJobsInOrderAndSurvivesFailures) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(200, 40, 40, 25));
    auto session = sessionWith(dir / "red.mp4", 64, 32);
    const std::string tl = session->timeline().id;
    // A project whose LUT is missing fails at export.
    Project broken = session->project();
    broken.timelines[0].outputLut = LutRef{dir / "missing.cube", {}};

    RenderQueue queue;
    std::atomic<int> notifications{0};
    queue.setListener([&] { ++notifications; });
    ExportOptions a;
    a.output = dir / "a.mp4";
    a.preset = preset("h264-web");
    ExportOptions b = a;
    b.output = dir / "b.mp4";
    ExportOptions c = a;
    c.output = dir / "c.wav";
    c.preset = preset("wav-24");
    ExportOptions d = a;
    d.output = dir / "d.mp4";
    const int first = queue.add(session->project(), tl, a, "First");
    const int failing = queue.add(broken, tl, b);
    const int third = queue.add(session->project(), tl, c);
    const int dropped = queue.add(session->project(), tl, d);
    EXPECT_TRUE(queue.cancel(dropped) || queue.job(dropped)->state != RenderQueue::State::Queued);
    ASSERT_TRUE(queue.waitIdle(std::chrono::seconds(120)));

    EXPECT_EQ(queue.job(first)->state, RenderQueue::State::Done);
    EXPECT_EQ(queue.job(first)->name, "First");
    EXPECT_EQ(queue.job(first)->framesDone, 25);
    EXPECT_EQ(queue.job(failing)->state, RenderQueue::State::Failed);
    EXPECT_NE(queue.job(failing)->message.find("missing.cube"), std::string::npos);
    EXPECT_EQ(queue.job(third)->state, RenderQueue::State::Done);  // a failure does not stop the queue
    EXPECT_TRUE(std::filesystem::exists(dir / "a.mp4"));
    EXPECT_FALSE(std::filesystem::exists(dir / "b.mp4"));
    EXPECT_TRUE(std::filesystem::exists(dir / "c.wav"));
    if (queue.job(dropped)->state == RenderQueue::State::Cancelled) {
        EXPECT_FALSE(std::filesystem::exists(dir / "d.mp4"));
    }
    EXPECT_GE(queue.job(first)->log.size(), 3u);  // queued, rendering, done
    EXPECT_GT(notifications.load(), 4);

    // Cancelling a running job stops it and leaves no file.
    test::makeMedia(dir / "long.mp4", test::solid(10, 200, 10, 250));
    auto longSession = sessionWith(dir / "long.mp4", 640, 360);
    ExportOptions slow;
    slow.output = dir / "long-out.mov";
    slow.preset = encoderAvailable("prores_ks") ? preset("prores-4444") : preset("h264-high");
    slow.output.replace_extension(slow.preset->extension);
    const int running = queue.add(longSession->project(), longSession->timeline().id, slow);
    for (int i = 0; i < 400 && queue.job(running)->framesDone < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(queue.job(running)->state, RenderQueue::State::Running);
    EXPECT_TRUE(queue.cancel(running));
    ASSERT_TRUE(queue.waitIdle(std::chrono::seconds(60)));
    EXPECT_EQ(queue.job(running)->state, RenderQueue::State::Cancelled);
    EXPECT_LT(queue.job(running)->framesDone, 250);
    EXPECT_FALSE(std::filesystem::exists(slow.output));

    // Finished jobs can be cleared; a job can be removed once it is not running.
    EXPECT_TRUE(queue.remove(first));
    queue.clearFinished();
    EXPECT_TRUE(queue.jobs().empty());
}

TEST(Delivery, EveryFrameKeepsTheStreamsYuvMatrix) {
    // Regression: swscale recreated its context between frames (at the same address), and
    // frames after the first two were converted with BT.601 instead of the tagged BT.709,
    // shifting (200, 40, 40) to about (187, 24, 42). Absolute values, several frames.
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(200, 40, 40, 12));  // 160x120 with audio
    auto session = sessionWith(dir / "red.mp4", 64, 32);             // decoded downscaled
    FrameCompositor compositor(resolverFor(session->project()));
    for (FrameIndex f : {0, 1, 2, 5, 8, 11}) {
        const VideoFrame frame = compositor.render(session->timeline(), f).value();
        const uint8_t* px = frame.row(16) + 32 * 4;
        EXPECT_NEAR(px[0], 200, 4) << "frame " << f;
        EXPECT_NEAR(px[1], 40, 4) << "frame " << f;
        EXPECT_NEAR(px[2], 40, 4) << "frame " << f;
    }
    ExportOptions web;
    web.output = dir / "web.mp4";
    web.preset = preset("h264-web");
    ASSERT_TRUE(ExportJob(session->project(), session->timeline().id, web).run().ok());
    auto decoder = VideoDecoder::open(web.output);
    ASSERT_TRUE(decoder.ok());
    for (double t : {0.0, 0.2, 0.44}) {
        const VideoFrame frame = decoder.value()->frameAt(t).value();
        EXPECT_NEAR(frame.row(16)[32 * 4], 200, 4) << "at " << t;
        EXPECT_NEAR(frame.row(16)[32 * 4 + 1], 40, 4) << "at " << t;
    }
}
