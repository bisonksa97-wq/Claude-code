// End-to-end test of the first vertical slice:
// create project -> import video -> place on timeline -> cut -> trim -> add second clip
// -> add audio -> save -> reopen -> export -> verify the rendered file.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "app/EditorSession.h"
#include "codec/AudioDecoder.h"
#include "codec/MediaProbe.h"
#include "codec/VideoDecoder.h"
#include "project/ProjectSerializer.h"
#include "render/AudioMixer.h"
#include "render/ColorManagement.h"
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

// Golden-value compositing through the real decoder: all video tracks are blended.
TEST(VerticalSlice, CompositorBlendsTracksWithTransforms) {
    test::TempDir dir;
    test::makeMedia(dir / "blue.mp4", test::solid(20, 20, 220, 25));
    test::makeMedia(dir / "red.mp4", test::solid(220, 20, 20, 25));
    auto session = EditorSession::createNew("Layers", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
    const auto ids = session->importMedia({dir / "blue.mp4", dir / "red.mp4"}).importedIds;
    const auto video = session->timeline().trackIdsOfKind(TrackKind::Video);
    ASSERT_TRUE(session->placeMedia(ids[0], 0, ops::EditMode::Overwrite, video[0]).ok());  // blue on V1
    ASSERT_TRUE(session->placeMedia(ids[1], 0, ops::EditMode::Overwrite, video[1]).ok());  // red on V2
    Timeline& tl = session->timeline();
    Clip& red = tl.tracks[1].clips.at(0);
    render::FrameCompositor compositor(render::resolverFor(session->project()));
    auto at = [&](FrameIndex f, int x, int y) {
        auto frame = compositor.render(tl, f);
        EXPECT_TRUE(frame.ok());
        const uint8_t* p = frame.value().row(y) + x * 4;
        return std::array<int, 3>{p[0], p[1], p[2]};
    };
    // Opaque full-frame V2 hides V1.
    EXPECT_GT(at(5, 80, 60)[0], 200);

    // Half-size V2 in the top-left quadrant: V1 shows around it.
    red.transform[ClipParam::Scale].value = 50;
    red.transform[ClipParam::PositionX].value = -40;
    red.transform[ClipParam::PositionY].value = -30;
    EXPECT_GT(at(5, 40, 30)[0], 200);   // centre of the red quadrant
    EXPECT_GT(at(5, 120, 90)[2], 200);  // blue elsewhere

    // Opacity fade keyframed from 0 at source frame 0 to 100 at frame 20.
    red.transform = ClipTransform{};
    red.transform[ClipParam::Opacity].setKey(0, 0);
    red.transform[ClipParam::Opacity].setKey(20, 100);
    const auto start = at(0, 80, 60);
    const auto mid = at(10, 80, 60);
    const auto end = at(20, 80, 60);
    EXPECT_GT(start[2], 200);               // fully blue
    EXPECT_NEAR(mid[0], (220 + 20) / 2, 12); // half way
    EXPECT_NEAR(mid[2], (220 + 20) / 2, 12);
    EXPECT_GT(end[0], 200);                 // fully red

    // Disabling V2 or making it transparent reveals V1 again.
    tl.tracks[1].enabled = false;
    EXPECT_GT(at(20, 80, 60)[2], 200);
}


namespace {

// red [0,50) using source 0..50 of 75 (25-frame tail handle, 440 Hz tone);
// blue [50,100) using source 25..75 of 75 (25-frame head handle, silent).
struct TransitionFixture {
    test::TempDir dir;
    std::unique_ptr<EditorSession> session;
    std::string redVideo, blueVideo, redAudio, blueAudio;

    TransitionFixture() {
        media::SyntheticSpec red = test::solid(220, 20, 20, 75, 440);
        red.toneLevel = 0.5f;
        test::makeMedia(dir / "red.mp4", red);
        test::makeMedia(dir / "blue.mp4", test::solid(20, 20, 220, 75, 0.0));
        session = EditorSession::createNew("Tx", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
        const auto ids = session->importMedia({dir / "red.mp4", dir / "blue.mp4"}).importedIds;
        EXPECT_TRUE(session->placeMedia(ids[0], 0, ops::EditMode::Overwrite, {}, {}, 0, 50).ok());
        EXPECT_TRUE(session->placeMedia(ids[1], 50, ops::EditMode::Overwrite, {}, {}, 25, 50).ok());
        const Timeline& tl = session->timeline();
        redVideo = tl.tracks[0].clips[0].id;
        blueVideo = tl.tracks[0].clips[1].id;
        redAudio = tl.tracks[2].clips[0].id;
        blueAudio = tl.tracks[2].clips[1].id;
    }

    test::Rgb color(FrameIndex f) {
        render::FrameCompositor compositor(render::resolverFor(session->project()));
        auto frame = compositor.render(session->timeline(), f);
        EXPECT_TRUE(frame.ok());
        return frame.ok() ? test::averageColor(frame.value()) : test::Rgb{};
    }
};

double windowRms(const std::vector<float>& s) {
    double sum = 0;
    for (float v : s) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(s.size()));
}

}  // namespace

TEST(Transitions, CrossDissolveAndDipRenderThroughTheDecoder) {
    TransitionFixture fx;
    ASSERT_TRUE(fx.session->setTransition(fx.blueVideo, ops::Edge::In,
                                          Transition{TransitionKind::Dissolve, 20, TransitionAlignment::Center}, false)
                    .ok());
    // Region [40,60): progress (f - 40 + 0.5) / 20.
    const auto before = fx.color(39);
    EXPECT_GT(before.r, 200);
    const auto start = fx.color(40);  // 2.5% blue
    EXPECT_GT(start.r, 190);
    const auto mid = fx.color(49);    // 47.5% blue, drawn from red's tail handle and blue's head handle
    EXPECT_NEAR(mid.r, 220 * 0.525 + 20 * 0.475, 10);
    EXPECT_NEAR(mid.b, 20 * 0.525 + 220 * 0.475, 10);
    EXPECT_GT(fx.color(59).b, 200);

    ASSERT_TRUE(fx.session->setTransition(fx.blueVideo, ops::Edge::In,
                                          Transition{TransitionKind::Dip, 20, TransitionAlignment::Center}, false)
                    .ok());
    const auto dip = fx.color(49);  // 95% of the way to black
    EXPECT_LT(dip.r, 25);
    EXPECT_LT(dip.b, 25);
    EXPECT_GT(fx.color(44).r, 70);  // still fading out of red
}

TEST(Transitions, FadeInFromBlackAndOutToBlack) {
    TransitionFixture fx;
    ASSERT_TRUE(fx.session->setTransition(fx.redVideo, ops::Edge::In, Transition{TransitionKind::Dissolve, 10}, false).ok());
    ASSERT_TRUE(fx.session->setTransition(fx.blueVideo, ops::Edge::Out, Transition{TransitionKind::Dissolve, 10}, false).ok());
    EXPECT_LT(fx.color(0).r, 20);          // 5% up
    EXPECT_NEAR(fx.color(4).r, 220 * 0.45, 12);
    EXPECT_GT(fx.color(10).r, 200);
    EXPECT_GT(fx.color(89).b, 200);
    EXPECT_LT(fx.color(99).b, 20);         // 95% down
}

TEST(Transitions, AudioCrossfadeIsConstantPowerAndPlaysIntoHandles) {
    TransitionFixture fx;
    ASSERT_TRUE(fx.session->setTransition(fx.blueAudio, ops::Edge::In, Transition{TransitionKind::Dissolve, 20}, false).ok());
    render::AudioMixer mixer(render::resolverFor(fx.session->project()));
    const Timeline& tl = fx.session->timeline();
    auto rmsAt = [&](FrameIndex f) {
        std::vector<float> buf;
        EXPECT_TRUE(mixer.mix(tl, frameToSample(f, tl.frameRate, 48000), 960, buf).ok());  // half a frame
        return windowRms(buf);
    };
    const double full = 0.5 / std::sqrt(2.0);
    EXPECT_NEAR(rmsAt(20), full, 0.02);
    // Red (outgoing) follows cos(p * pi/2) across [40, 60); blue is silent.
    EXPECT_NEAR(rmsAt(50), full * std::cos(0.5 * 1.5707963), 0.03);  // plays past its cut into the handle
    EXPECT_NEAR(rmsAt(44), full * std::cos(0.2 * 1.5707963), 0.03);
    EXPECT_LT(rmsAt(60), 0.005);
}

TEST(Transitions, SessionValidatesHandlesAndLinksAudio) {
    TransitionFixture fx;
    // Blue has a 25-frame head handle, red a 25-frame tail handle: centred, at most 50 frames.
    auto tooLong = fx.session->setTransition(fx.blueVideo, ops::Edge::In, Transition{TransitionKind::Dissolve, 60});
    ASSERT_FALSE(tooLong.ok());
    EXPECT_NE(tooLong.error().message.find("at most 50"), std::string::npos);
    // Linked audio gets the matching crossfade in the same undo step.
    ASSERT_TRUE(fx.session->setTransition(fx.blueVideo, ops::Edge::In, Transition{TransitionKind::Dissolve, 30}).ok());
    const Timeline& tl = fx.session->timeline();
    EXPECT_EQ(tl.clip(fx.blueAudio)->transitionIn->duration, 30);
    fx.session->undo();
    EXPECT_FALSE(tl.clip(fx.blueAudio)->transitionIn.has_value());
    EXPECT_FALSE(tl.clip(fx.blueVideo)->transitionIn.has_value());

    // The default transition picks the nearest edge and fits the available media.
    auto applied = fx.session->applyDefaultTransition(fx.blueVideo, 52, TransitionKind::Dissolve, 80);
    ASSERT_TRUE(applied.ok()) << applied.error().toString();
    EXPECT_EQ(applied.value(), 50);
    auto tail = fx.session->applyDefaultTransition(fx.blueVideo, 95);
    ASSERT_TRUE(tail.ok());
    EXPECT_EQ(tl.clip(fx.blueVideo)->transitionOut->duration, 25);  // one second at 25 fps
    ASSERT_TRUE(fx.session->setTransition(fx.blueVideo, ops::Edge::Out, std::nullopt).ok());
    EXPECT_FALSE(tl.clip(fx.blueAudio)->transitionOut.has_value());
}

TEST(Transitions, ExportedDissolve) {
    TransitionFixture fx;
    ASSERT_TRUE(fx.session->setTransition(fx.blueVideo, ops::Edge::In, Transition{TransitionKind::Dissolve, 20}).ok());
    render::ExportOptions options;
    options.output = fx.dir / "dissolve.mp4";
    ASSERT_TRUE(render::ExportJob(fx.session->project(), fx.session->timeline().id, options).run().ok());
    auto dec = VideoDecoder::open(options.output);
    ASSERT_TRUE(dec.ok());
    const auto mid = test::averageColor(dec.value()->frameAt(49 / 25.0 + 0.001).value());
    EXPECT_NEAR(mid.r, 220 * 0.525 + 20 * 0.475, 12);
    EXPECT_NEAR(mid.b, 20 * 0.525 + 220 * 0.475, 12);
}

namespace {

// One 2-second 440 Hz clip (0.5 amplitude) on V1/A1.
struct MixFixture {
    test::TempDir dir;
    std::unique_ptr<EditorSession> session;
    std::string audioClip, a1;

    MixFixture() {
        media::SyntheticSpec tone = test::solid(0, 0, 0, 50, 440);
        tone.toneLevel = 0.5f;
        test::makeMedia(dir / "tone.mp4", tone);
        session = EditorSession::createNew("Mix", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
        const auto ids = session->importMedia({dir / "tone.mp4"}).importedIds;
        EXPECT_TRUE(session->appendMedia(ids[0]).ok());
        audioClip = session->timeline().tracks[2].clips[0].id;
        a1 = session->timeline().tracks[2].id;
    }

    // Left/right RMS of 0.1 s starting at `frame`, plus meters.
    std::pair<double, double> levels(FrameIndex frame, render::MixMeters* meters = nullptr) {
        render::AudioMixer mixer(render::resolverFor(session->project()));
        std::vector<float> buf;
        EXPECT_TRUE(mixer.mix(session->timeline(), frameToSample(frame, FrameRate{25, 1}, 48000), 4800, buf, meters).ok());
        double l = 0, r = 0;
        for (std::size_t i = 0; i < buf.size(); i += 2) {
            l += static_cast<double>(buf[i]) * buf[i];
            r += static_cast<double>(buf[i + 1]) * buf[i + 1];
        }
        return {std::sqrt(l / 4800), std::sqrt(r / 4800)};
    }
};

}  // namespace

TEST(Mixing, TrackPanGainAndMeters) {
    MixFixture fx;
    const double full = 0.5 / std::sqrt(2.0);
    auto [l, r] = fx.levels(10);
    EXPECT_NEAR(l, full, 0.02);
    EXPECT_NEAR(r, full, 0.02);

    TrackState state = TrackState::of(*fx.session->timeline().track(fx.a1));
    state.pan = -1.0;
    state.gainDb = -6.0;
    ASSERT_TRUE(fx.session->setTrackState(fx.a1, state).ok());
    render::MixMeters meters;
    std::tie(l, r) = fx.levels(10, &meters);
    EXPECT_NEAR(l, full * 0.501, 0.02);  // -6 dB
    EXPECT_LT(r, 0.001);                 // hard left
    ASSERT_EQ(meters.tracks.count(fx.a1), 1u);
    EXPECT_NEAR(meters.tracks[fx.a1].peakLeft, 0.25, 0.03);
    EXPECT_LT(meters.tracks[fx.a1].peakRight, 0.001);
    EXPECT_NEAR(meters.master.peakLeft, 0.25, 0.03);

    state.pan = 2.0;
    EXPECT_FALSE(fx.session->setTrackState(fx.a1, state).ok());
}

TEST(Mixing, ClipVolumeAndPanAutomation) {
    MixFixture fx;
    // Fade the clip's volume from -60 dB at frame 0 to 0 dB at frame 40.
    ASSERT_TRUE(fx.session->setKeyframe(fx.audioClip, ClipParam::Volume, 0, true).ok());
    ASSERT_TRUE(fx.session->setClipParameter(fx.audioClip, ClipParam::Volume, -60, 0).ok());
    ASSERT_TRUE(fx.session->setKeyframe(fx.audioClip, ClipParam::Volume, 40, true).ok());
    ASSERT_TRUE(fx.session->setClipParameter(fx.audioClip, ClipParam::Volume, 0, 40).ok());
    const double full = 0.5 / std::sqrt(2.0);
    EXPECT_LT(fx.levels(0).first, full * 0.05);
    // The 0.1 s window from frame 18 centres on ~19.25: -60 + 60 * 19.25 / 40 = about -31 dB.
    EXPECT_NEAR(fx.levels(18).first, full * std::pow(10.0, -31.0 / 20.0), full * 0.01);
    EXPECT_NEAR(fx.levels(42).first, full, 0.02);
    // Constant clip pan to the right.
    ASSERT_TRUE(fx.session->setClipParameter(fx.audioClip, ClipParam::Pan, 100, 0).ok());
    const auto [l, r] = fx.levels(42);
    EXPECT_LT(l, 0.001);
    EXPECT_NEAR(r, full, 0.02);
    // Audio parameters only on audio clips.
    const std::string video = fx.session->timeline().tracks[0].clips[0].id;
    EXPECT_EQ(fx.session->setClipParameter(video, ClipParam::Volume, -6, 0).error().code, ErrorCode::InvalidArgument);
}

TEST(Mixing, TrackEffectsProcessTheBusAndKeepStateAcrossCalls) {
    MixFixture fx;
    auto gain = fx.session->addTrackEffect(fx.a1, "gain");
    ASSERT_TRUE(gain.ok());
    audio::EffectSpec g = fx.session->timeline().track(fx.a1)->effects[0];
    g.params["gain"] = -12;
    ASSERT_TRUE(fx.session->updateTrackEffect(fx.a1, g).ok());
    const double full = 0.5 / std::sqrt(2.0);
    EXPECT_NEAR(fx.levels(10).first, full * 0.251, 0.01);
    g.enabled = false;
    ASSERT_TRUE(fx.session->updateTrackEffect(fx.a1, g).ok());
    EXPECT_NEAR(fx.levels(10).first, full, 0.02);

    // A compressor mixed in two consecutive calls matches one long call (state carried over).
    ASSERT_TRUE(fx.session->removeTrackEffect(fx.a1, g.id).ok());
    ASSERT_TRUE(fx.session->addTrackEffect(fx.a1, "compressor").ok());
    const Timeline& tl = fx.session->timeline();
    render::AudioMixer one(render::resolverFor(fx.session->project()));
    render::AudioMixer two(render::resolverFor(fx.session->project()));
    std::vector<float> whole, a, b;
    ASSERT_TRUE(one.mix(tl, 0, 9600, whole).ok());
    ASSERT_TRUE(two.mix(tl, 0, 4800, a).ok());
    ASSERT_TRUE(two.mix(tl, 4800, 4800, b).ok());
    a.insert(a.end(), b.begin(), b.end());
    double diff = 0;
    for (std::size_t i = 0; i < whole.size(); ++i) diff = std::max(diff, static_cast<double>(std::abs(whole[i] - a[i])));
    EXPECT_LT(diff, 1e-6);

    // Chain editing: order, validation, wrong track kind, undo.
    auto eq = fx.session->addTrackEffect(fx.a1, "eq3");
    ASSERT_TRUE(eq.ok());
    ASSERT_TRUE(fx.session->moveTrackEffect(fx.a1, eq.value(), 0).ok());
    EXPECT_EQ(tl.track(fx.a1)->effects[0].type, "eq3");
    audio::EffectSpec bad = tl.track(fx.a1)->effects[0];
    bad.params["midQ"] = 50;
    EXPECT_EQ(fx.session->updateTrackEffect(fx.a1, bad).error().code, ErrorCode::OutOfRange);
    EXPECT_FALSE(fx.session->addTrackEffect(tl.tracks[0].id, "gain").ok());  // video track
    EXPECT_FALSE(fx.session->addTrackEffect(fx.a1, "reverb").ok());
    fx.session->undo();
    EXPECT_EQ(tl.track(fx.a1)->effects[0].type, "compressor");
}

TEST(Color, GradesRenderPersistAndCopy) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(200, 50, 50, 25));
    auto session = EditorSession::createNew("Grade", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
    const auto ids = session->importMedia({dir / "red.mp4"}).importedIds;
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    const Timeline& tl = session->timeline();
    const std::string first = tl.tracks[0].clips[0].id;
    const std::string second = tl.tracks[0].clips[1].id;
    auto colorAt = [&](FrameIndex f) {
        render::FrameCompositor compositor(render::resolverFor(session->project()));
        return test::averageColor(compositor.render(tl, f).value());
    };
    const auto original = colorAt(5);

    // Saturation 0 turns the red clip grey (Rec.709 luma kept); undo restores it.
    ASSERT_TRUE(session->setGradeParameter(first, GradeParam::Saturation, 0, 5).ok());
    auto grey = colorAt(5);
    EXPECT_NEAR(grey.r, grey.g, 2);
    EXPECT_NEAR(grey.g, grey.b, 2);
    EXPECT_NEAR(grey.r, 0.2126 * original.r + 0.7152 * original.g + 0.0722 * original.b, 3);
    EXPECT_GT(colorAt(30).r, 180);  // the second clip is untouched

    // Keyframed gain: dark at the clip's first frame, normal at frame 20.
    ASSERT_TRUE(session->setGradeKeyframe(first, GradeParam::GainMaster, 0, true).ok());
    ASSERT_TRUE(session->setGradeParameter(first, GradeParam::GainMaster, 0.0, 0).ok());
    ASSERT_TRUE(session->setGradeKeyframe(first, GradeParam::GainMaster, 20, true).ok());
    ASSERT_TRUE(session->setGradeParameter(first, GradeParam::GainMaster, 1.0, 20).ok());
    EXPECT_LT(colorAt(0).r, 5);
    EXPECT_NEAR(colorAt(10).r, grey.r * 0.5, 6);

    // Copy/paste onto the second clip in one undo step; the audio half of a selection is skipped.
    ASSERT_TRUE(session->copyGrade(first).ok());
    ASSERT_TRUE(session->pasteGrade({second, tl.tracks[2].clips[1].id}).ok());
    EXPECT_FALSE(tl.clip(second)->grade.isIdentity());
    EXPECT_FALSE(session->copyGrade(tl.tracks[2].clips[0].id).ok());
    session->undo();
    EXPECT_TRUE(tl.clip(second)->grade.isIdentity());

    // Save and reopen keeps the grade; resetting clears it.
    ASSERT_TRUE(session->saveAs(dir / "g.uproj").ok());
    auto reopened = EditorSession::open(dir / "g.uproj");
    ASSERT_TRUE(reopened.ok());
    const Clip& loaded = reopened.value()->timeline().tracks[0].clips[0];
    EXPECT_EQ(loaded.grade[GradeParam::Saturation].value, 0);
    EXPECT_EQ(loaded.grade[GradeParam::GainMaster].keys.size(), 2u);
    ASSERT_TRUE(session->resetGrade(first).ok());
    EXPECT_TRUE(tl.clip(first)->grade.isIdentity());
    EXPECT_EQ(session->setGradeParameter(tl.tracks[2].clips[0].id, GradeParam::Contrast, 2, 5).error().code,
              ErrorCode::InvalidArgument);
}


TEST(Color, CurvesLutsBypassAndVersions) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(200, 50, 50, 25));
    const auto swapLut = dir / "swap.cube";
    test::writeText(swapLut, test::cubeText(9, [](float r, float g, float b) { return std::array<float, 3>{g, b, r}; }));
    const auto invertLut = dir / "invert.cube";
    test::writeText(invertLut, test::cubeText(5, [](float r, float g, float b) { return std::array<float, 3>{1 - r, 1 - g, 1 - b}; }, false));
    test::writeText(dir / "broken.cube", "LUT_3D_SIZE 2\n0 0 0\n");
    auto session = EditorSession::createNew("Looks", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
    const auto ids = session->importMedia({dir / "red.mp4"}).importedIds;
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    ASSERT_TRUE(session->appendMedia(ids[0]).ok());
    const Timeline& tl = session->timeline();
    const std::string clip = tl.tracks[0].clips[0].id;
    const std::string other = tl.tracks[0].clips[1].id;
    render::FrameCompositor compositor(render::resolverFor(session->project()));
    auto colorAt = [&](FrameIndex f) { return test::averageColor(compositor.render(tl, f).value()); };
    const auto original = colorAt(5);

    // A red curve halving red.
    ASSERT_TRUE(session->setGradeCurve(clip, CurveKind::Red, {{1, 0.5}, {0, 0}}).ok());
    EXPECT_DOUBLE_EQ(tl.clip(clip)->grade.curve(CurveKind::Red)[0].x, 0.0);  // stored sorted
    EXPECT_NEAR(colorAt(5).r, original.r / 2, 3);
    EXPECT_FALSE(session->setGradeCurve(clip, CurveKind::Red, {{0.5, 0.5}}).ok());

    // A LUT after the curve: channels rotate (r, g, b) -> (g, b, r).
    ASSERT_TRUE(session->setGradeLut(clip, swapLut).ok());
    auto swapped = colorAt(5);
    EXPECT_NEAR(swapped.r, original.g, 3);
    EXPECT_NEAR(swapped.b, original.r / 2, 3);
    auto broken = session->setGradeLut(clip, dir / "broken.cube");
    ASSERT_FALSE(broken.ok());
    EXPECT_NE(broken.error().message.find("broken.cube"), std::string::npos);
    EXPECT_EQ(tl.clip(clip)->grade.lut->path, swapLut);  // unchanged

    // Bypass per clip and for the whole timeline (both undoable).
    ASSERT_TRUE(session->setGradeBypass(clip, true).ok());
    EXPECT_NEAR(colorAt(5).r, original.r, 2);
    session->undo();
    ASSERT_TRUE(session->setGradesBypassed(true).ok());
    EXPECT_NEAR(colorAt(5).r, original.r, 2);
    session->undo();
    EXPECT_NEAR(colorAt(5).r, swapped.r, 2);

    // An output LUT applies to the whole picture, after the clip grades.
    ASSERT_TRUE(session->setOutputLut(invertLut).ok());
    EXPECT_NEAR(colorAt(30).r, 255 - original.r, 3);
    EXPECT_NEAR(colorAt(5).r, 255 - swapped.r, 3);
    ASSERT_TRUE(session->setOutputLut(std::nullopt).ok());

    // Versions: B starts as a copy of A; resetting B leaves A intact; switching back restores it.
    ASSERT_TRUE(session->addGradeVersion(clip, "B").ok());
    EXPECT_EQ(tl.clip(clip)->gradeVersion, "B");
    EXPECT_NEAR(colorAt(5).r, swapped.r, 2);
    ASSERT_TRUE(session->resetGrade(clip).ok());
    EXPECT_NEAR(colorAt(5).r, original.r, 2);
    EXPECT_EQ(session->addGradeVersion(clip, "A").error().code, ErrorCode::Conflict);
    EXPECT_FALSE(session->deleteGradeVersion(clip, "B").ok());  // active
    ASSERT_TRUE(session->selectGradeVersion(clip, "A").ok());
    EXPECT_NEAR(colorAt(5).r, swapped.r, 2);
    ASSERT_EQ(tl.clip(clip)->gradeVersions.size(), 1u);
    EXPECT_EQ(tl.clip(clip)->gradeVersions[0].name, "B");
    ASSERT_TRUE(session->deleteGradeVersion(clip, "B").ok());
    session->undo();
    EXPECT_EQ(tl.clip(clip)->gradeVersions.size(), 1u);

    // Paste carries curves and the LUT.
    ASSERT_TRUE(session->copyGrade(clip).ok());
    ASSERT_TRUE(session->pasteGrade({other}).ok());
    EXPECT_NEAR(colorAt(30).r, swapped.r, 3);

    // A missing LUT: listed, skipped when viewing, refused by export; relinking fixes it.
    const auto movedLut = dir / "moved" / "swap.cube";
    std::filesystem::create_directories(movedLut.parent_path());
    std::filesystem::rename(swapLut, movedLut);
    EXPECT_EQ(session->missingLuts(), std::vector<std::filesystem::path>{swapLut});
    render::FrameCompositor fresh(render::resolverFor(session->project()));
    EXPECT_NEAR(test::averageColor(fresh.render(tl, 5).value()).r, original.r / 2, 3);  // curve only
    render::ExportOptions options;
    options.output = dir / "out.mp4";
    auto exported = render::ExportJob(session->project(), tl.id, options).run();
    ASSERT_FALSE(exported.ok());
    EXPECT_NE(exported.error().message.find("swap.cube"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(options.output));
    auto relinked = session->relinkLut(swapLut, movedLut);
    ASSERT_TRUE(relinked.ok()) << relinked.error().toString();
    EXPECT_EQ(relinked.value(), 2);  // both clips (version B was reset, so it has no LUT)
    EXPECT_TRUE(session->missingLuts().empty());
    EXPECT_FALSE(session->relinkLut(swapLut, movedLut).ok());  // nothing uses the old path now
    EXPECT_TRUE(render::checkTimelineLuts(tl).ok());
    EXPECT_NEAR(colorAt(5).r, swapped.r, 2);
}

TEST(ColorManagement, TimelineMediaAndOutputSpacesThroughTheRenderer) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(200, 40, 40, 75));  // handles for the dissolve
    test::makeMedia(dir / "blue.mp4", test::solid(40, 40, 200, 75));
    auto session = EditorSession::createNew("CM", SequenceSettings{FrameRate{25, 1}, 160, 120, 48000});
    const auto ids = session->importMedia({dir / "red.mp4", dir / "blue.mp4"}).importedIds;
    ASSERT_TRUE(session->placeMedia(ids[0], 0, ops::EditMode::Overwrite, {}, {}, 0, 50).ok());
    ASSERT_TRUE(session->placeMedia(ids[1], 50, ops::EditMode::Overwrite, {}, {}, 25, 50).ok());
    const Timeline& tl = session->timeline();
    const std::string blue = tl.tracks[0].clips[1].id;
    // Our own exports are tagged, so they are detected as Rec.709 (gamma 2.4) and pass straight through.
    const MediaItem& red = *session->project().findMedia(ids[0]);
    EXPECT_EQ(red.info.colorPrimaries, "bt709");
    EXPECT_EQ(mediaColorSpace(red), (ColorSpace{Primaries::Rec709, Transfer::BT1886}));
    render::FrameCompositor compositor(render::resolverFor(session->project()));
    auto colorAt = [&](FrameIndex f) { return test::averageColor(compositor.render(tl, f).value()); };
    const auto original = colorAt(10);
    EXPECT_NEAR(original.r, 200, 3);

    // A linear timeline shown as linear is dark; with a Rec.709 output it round-trips.
    ASSERT_TRUE(session->setTimelineColorSpace({Primaries::Rec709, Transfer::Linear}).ok());
    EXPECT_NEAR(colorAt(10).r, 255 * std::pow(original.r / 255, 2.4), 3);
    ASSERT_TRUE(session->setOutputColorSpace(ColorSpace{Primaries::Rec709, Transfer::BT1886}).ok());
    EXPECT_NEAR(colorAt(10).r, original.r, 1.5);
    EXPECT_NEAR(colorAt(10).g, original.g, 1.5);

    // Dissolves mix in the timeline space: in linear light the midpoint is brighter.
    ASSERT_TRUE(session->setTransition(blue, ops::Edge::In, Transition{TransitionKind::Dissolve, 20, TransitionAlignment::Center}).ok());
    const auto linearMid = colorAt(50);
    ASSERT_TRUE(session->setTimelineColorSpace({Primaries::Rec709, Transfer::BT1886}).ok());
    ASSERT_TRUE(session->setOutputColorSpace(std::nullopt).ok());
    const auto encodedMid = colorAt(50);
    EXPECT_GT(linearMid.r, encodedMid.r + 20);
    EXPECT_GT(linearMid.b, encodedMid.b + 20);

    // A media override reinterprets the pixels: sRGB material on a gamma 2.4 timeline.
    ASSERT_TRUE(session->setMediaColorSpace(ids[0], ColorSpace{Primaries::Rec709, Transfer::SRGB}).ok());
    const double expected = 255 * std::pow(render::decodeTransfer(Transfer::SRGB, original.r / 255), 1 / 2.4);
    EXPECT_NEAR(colorAt(10).r, expected, 1.5);
    session->undo();
    EXPECT_NEAR(colorAt(10).r, original.r, 1.5);
    EXPECT_EQ(session->setMediaColorSpace("nope", std::nullopt).error().code, ErrorCode::NotFound);

    // Export tags the file with the output space, and the pixels follow it.
    render::ExportOptions options;
    options.output = dir / "hdr.mp4";
    options.includeAudio = false;
    ASSERT_TRUE(session->setOutputColorSpace(ColorSpace{Primaries::Rec2020, Transfer::PQ}).ok());
    ASSERT_TRUE(render::ExportJob(session->project(), tl.id, options).run().ok());
    auto probed = probeMedia(options.output);
    ASSERT_TRUE(probed.ok());
    EXPECT_EQ(probed.value().colorPrimaries, "bt2020");
    EXPECT_EQ(probed.value().colorTransfer, "smpte2084");
    EXPECT_EQ(probed.value().colorMatrix, "bt2020nc");
    EXPECT_EQ(detectColorSpace(probed.value().colorPrimaries, probed.value().colorTransfer, false),
              (ColorSpace{Primaries::Rec2020, Transfer::PQ}));
    // Decoding the HDR file with its own (BT.2020) matrix gives back the rendered PQ values.
    const auto rendered = colorAt(10);
    auto decoder = VideoDecoder::open(options.output);
    ASSERT_TRUE(decoder.ok());
    const auto decoded = test::averageColor(decoder.value()->frameAt(0.4).value());
    EXPECT_NEAR(decoded.r, rendered.r, 3);
    EXPECT_NEAR(decoded.g, rendered.g, 3);
    EXPECT_NEAR(decoded.b, rendered.b, 3);
}
