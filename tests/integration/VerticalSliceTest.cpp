// End-to-end test of the first vertical slice:
// create project -> import video -> place on timeline -> cut -> trim -> add second clip
// -> add audio -> save -> reopen -> export -> verify the rendered file.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "app/EditorSession.h"
#include "codec/AudioDecoder.h"
#include "codec/MediaProbe.h"
#include "codec/VideoDecoder.h"
#include "project/ProjectSerializer.h"
#include "render/ExportJob.h"
#include "render/FrameCompositor.h"
#include "support/TestSupport.h"

using namespace up;

namespace {

double rms(AudioDecoder& dec, double fromSeconds, double seconds) {
    const auto n = static_cast<int64_t>(seconds * dec.sampleRate());
    std::vector<float> buf(static_cast<std::size_t>(n) * 2);
    EXPECT_TRUE(dec.read(static_cast<int64_t>(fromSeconds * dec.sampleRate()), n, buf.data()).ok());
    double sum = 0;
    for (float v : buf) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(buf.size()));
}

test::Rgb colorAt(VideoDecoder& dec, FrameIndex frame, FrameRate rate) {
    auto f = dec.frameAt(framesToSeconds(frame, rate) + 0.001);
    EXPECT_TRUE(f.ok());
    return f.ok() ? test::averageColor(f.value()) : test::Rgb{};
}

}  // namespace

TEST(VerticalSlice, CreateEditSaveReopenExport) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(220, 20, 20, 50, 440));
    test::makeMedia(dir / "blue.mp4", test::solid(20, 20, 220, 40, 440));
    media::SyntheticSpec music = test::solid(0, 0, 0, 100, 220);
    music.video = false;
    test::makeMedia(dir / "music.m4a", music);

    // Create project and import.
    auto session = EditorSession::createNew("Slice", SequenceSettings{FrameRate{25, 1}, 320, 240, 48000});
    auto report = session->importMedia({dir / "red.mp4", dir / "blue.mp4", dir / "music.m4a"});
    ASSERT_EQ(report.importedIds.size(), 3u);
    const std::string red = report.importedIds[0], blue = report.importedIds[1], musicId = report.importedIds[2];
    EXPECT_FALSE(session->project().findMedia(musicId)->info.hasVideo);

    // Edit: place red, cut at 25, trim the second half's tail by 5 frames, append blue.
    ASSERT_TRUE(session->placeMedia(red, 0, ops::EditMode::Overwrite).ok());
    ASSERT_EQ(session->razorAt(25).value(), 2);
    const std::string secondHalf = session->timeline().tracks[0].clips[1].id;
    ASSERT_TRUE(session->trimClip(secondHalf, ops::Edge::Out, -5, ops::TrimMode::Normal).ok());
    ASSERT_TRUE(session->appendMedia(blue).ok());
    EXPECT_EQ(session->timeline().duration(), 85);  // 45 frames of red + 40 of blue

    // Add audio-only media on A2 under the first second.
    const std::string a2 = session->timeline().trackIdsOfKind(TrackKind::Audio)[1];
    auto placed = session->placeMedia(musicId, 0, ops::EditMode::Overwrite, {}, a2, 0, 25);
    ASSERT_TRUE(placed.ok());
    EXPECT_EQ(placed.value().size(), 1u);

    // Save, reopen, and compare.
    const auto projectFile = dir / "slice.uproj";
    ASSERT_TRUE(session->saveAs(projectFile).ok());
    auto reopened = EditorSession::open(projectFile);
    ASSERT_TRUE(reopened.ok()) << reopened.error().toString();
    EXPECT_EQ(ProjectSerializer::toJson(reopened.value()->project(), projectFile),
              ProjectSerializer::toJson(session->project(), projectFile));
    for (const auto& m : reopened.value()->project().media) EXPECT_TRUE(m.online);

    // Export.
    render::ExportOptions options;
    options.output = dir / "out.mp4";
    render::ExportJob job(reopened.value()->project(), reopened.value()->timeline().id, options);
    FrameIndex lastProgress = 0;
    const Status exported = job.run([&](const render::ExportProgress& p) { lastProgress = p.framesDone; });
    ASSERT_TRUE(exported.ok()) << exported.error().toString();
    EXPECT_EQ(lastProgress, 85);
    EXPECT_FALSE(std::filesystem::exists(dir / "out.partial.mp4"));

    // Verify the output: format, picture per edit, audio presence.
    auto info = probeMedia(options.output);
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().width, 320);
    EXPECT_EQ(info.value().height, 240);
    EXPECT_NEAR(info.value().durationSeconds, 85 / 25.0, 0.1);
    EXPECT_TRUE(info.value().hasAudio);

    auto video = VideoDecoder::open(options.output);
    ASSERT_TRUE(video.ok());
    const FrameRate rate{25, 1};
    for (FrameIndex f : {0, 24, 25, 44}) {
        const auto c = colorAt(*video.value(), f, rate);
        EXPECT_GT(c.r, 180) << "frame " << f;
        EXPECT_LT(c.b, 60) << "frame " << f;
    }
    for (FrameIndex f : {45, 60, 84}) {
        const auto c = colorAt(*video.value(), f, rate);
        EXPECT_GT(c.b, 180) << "frame " << f;
        EXPECT_LT(c.r, 60) << "frame " << f;
    }

    auto audio = AudioDecoder::open(options.output, 48000, 2);
    ASSERT_TRUE(audio.ok());
    // First second: 440 Hz tone (0.25) + 220 Hz music (0.25) -> RMS ~ sqrt(2 * 0.25^2 / 2) = 0.25.
    EXPECT_NEAR(rms(*audio.value(), 0.2, 0.6), 0.25, 0.04);
    // After the music ends only one tone remains: RMS ~ 0.177.
    EXPECT_NEAR(rms(*audio.value(), 2.0, 1.0), 0.177, 0.03);
}

TEST(VerticalSlice, ExportHonoursMuteAndOfflineMedia) {
    test::TempDir dir;
    test::makeMedia(dir / "clip.mp4", test::solid(20, 220, 20, 25, 440));
    auto session = EditorSession::createNew("Mute", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
    const auto ids = session->importMedia({dir / "clip.mp4"}).importedIds;
    ASSERT_EQ(ids.size(), 1u);
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    const Track& a1 = session->timeline().tracks[2];
    TrackState muted;
    muted.muted = true;
    ASSERT_TRUE(session->setTrackState(a1.id, muted).ok());

    render::ExportOptions options;
    options.output = dir / "muted.mp4";
    ASSERT_TRUE(render::ExportJob(session->project(), session->timeline().id, options).run().ok());
    auto audio = AudioDecoder::open(options.output, 48000, 2);
    ASSERT_TRUE(audio.ok());
    EXPECT_LT(rms(*audio.value(), 0.2, 0.5), 0.001);

    // Offline media renders as the offline colour instead of failing silently.
    session->project().media[0].online = false;
    render::FrameCompositor compositor(render::resolverFor(session->project()));
    auto frame = compositor.render(session->timeline(), 5);
    ASSERT_TRUE(frame.ok());
    EXPECT_EQ(frame.value().pixels[0], render::FrameCompositor::kOfflineColor[0]);
}

TEST(VerticalSlice, ExportCanBeCancelledWithoutLeavingFiles) {
    test::TempDir dir;
    test::makeMedia(dir / "clip.mp4", test::solid(20, 220, 20, 25));
    auto session = EditorSession::createNew("Cancel", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
    const auto ids = session->importMedia({dir / "clip.mp4"}).importedIds;
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    render::ExportOptions options;
    options.output = dir / "cancelled.mp4";
    render::ExportJob job(session->project(), session->timeline().id, options);
    const Status s = job.run([&](const render::ExportProgress& p) {
        if (p.framesDone == 5) job.cancel();
    });
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, ErrorCode::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(options.output));
    EXPECT_FALSE(std::filesystem::exists(dir / "cancelled.partial.mp4"));
}

TEST(VerticalSlice, CompositorLetterboxesMismatchedAspect) {
    test::TempDir dir;
    media::SyntheticSpec square = test::solid(250, 250, 250, 5);
    square.width = 120;
    square.height = 120;
    test::makeMedia(dir / "square.mp4", square);
    auto session = EditorSession::createNew("Aspect", SequenceSettings{FrameRate{25, 1}, 320, 180, 48000});
    const auto ids = session->importMedia({dir / "square.mp4"}).importedIds;
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    render::FrameCompositor compositor(render::resolverFor(session->project()));
    auto frame = compositor.render(session->timeline(), 2);
    ASSERT_TRUE(frame.ok());
    const VideoFrame& f = frame.value();
    EXPECT_LT(f.row(90)[0], 10);                                  // left pillar is black
    EXPECT_GT(f.row(90)[static_cast<std::size_t>(160 * 4)], 230);  // centre shows the source
}
