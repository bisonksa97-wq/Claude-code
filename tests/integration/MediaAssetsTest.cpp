#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

#include "app/EditorSession.h"
#include "app/MediaAssets.h"
#include "codec/MediaProbe.h"
#include "media/MediaAnalysis.h"
#include "support/TestSupport.h"

using namespace up;
namespace fs = std::filesystem;

namespace {

MediaInfo probe(const fs::path& p) {
    auto info = probeMedia(p);
    EXPECT_TRUE(info.ok());
    return info.ok() ? info.value() : MediaInfo{};
}

}  // namespace

TEST(MediaAnalysis, ThumbnailShowsTheClipAndFitsTheBox) {
    test::TempDir dir;
    media::SyntheticSpec spec = test::solid(220, 20, 20, 50);
    spec.width = 320;
    spec.height = 240;
    test::makeMedia(dir / "red.mp4", spec);
    auto thumb = media::generateThumbnail(dir / "red.mp4", probe(dir / "red.mp4"), 192, 108);
    ASSERT_TRUE(thumb.ok()) << thumb.error().toString();
    EXPECT_EQ(thumb.value().height, 108);
    EXPECT_EQ(thumb.value().width, 144);  // 4:3 inside 192x108
    EXPECT_GT(test::averageColor(thumb.value()).r, 180);

    // PPM round trip.
    auto decoded = media::decodePpm(media::encodePpm(thumb.value()));
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().width, 144);
    EXPECT_EQ(decoded.value().pixels[0], thumb.value().pixels[0]);
    EXPECT_FALSE(media::decodePpm("P6\n10 10\n255\nshort").ok());
    EXPECT_FALSE(media::decodePpm("garbage").ok());
}

TEST(MediaAnalysis, ThumbnailRejectsAudioOnlyMedia) {
    test::TempDir dir;
    media::SyntheticSpec spec = test::solid(0, 0, 0, 25);
    spec.video = false;
    test::makeMedia(dir / "a.m4a", spec);
    EXPECT_FALSE(media::generateThumbnail(dir / "a.m4a", probe(dir / "a.m4a")).ok());
}

TEST(MediaAnalysis, WaveformEnvelopeMatchesTheTone) {
    test::TempDir dir;
    media::SyntheticSpec loud = test::solid(0, 0, 0, 50, 440);  // 2 s at level 0.25
    loud.toneLevel = 0.5f;
    test::makeMedia(dir / "loud.mp4", loud);
    const MediaInfo info = probe(dir / "loud.mp4");
    auto peaks = media::generateWaveform(dir / "loud.mp4", info);
    ASSERT_TRUE(peaks.ok()) << peaks.error().toString();
    const auto& w = peaks.value();
    EXPECT_EQ(w.samplesPerPeak, 480);
    EXPECT_NEAR(static_cast<double>(w.peakCount()), info.durationSeconds * 100, 2);
    const auto [lo, hi] = w.range(0.5, 1.5);
    EXPECT_NEAR(hi, 0.5, 0.05);
    EXPECT_NEAR(lo, -0.5, 0.05);
    EXPECT_EQ(w.range(100, 101), (std::pair<float, float>{0.0f, 0.0f}));  // outside the media

    // Silence stays silent (and is kept, not trimmed away).
    test::makeMedia(dir / "quiet.mp4", test::solid(0, 0, 0, 50, 0.0));
    const MediaInfo quietInfo = probe(dir / "quiet.mp4");
    auto quiet = media::generateWaveform(dir / "quiet.mp4", quietInfo);
    ASSERT_TRUE(quiet.ok());
    EXPECT_NEAR(static_cast<double>(quiet.value().peakCount()), quietInfo.durationSeconds * 100, 2);
    EXPECT_LT(quiet.value().range(0, 2).second, 0.01f);

    // Binary round trip and corruption detection.
    const std::string bytes = media::encodeWaveform(w);
    auto decoded = media::decodeWaveform(bytes);
    ASSERT_TRUE(decoded.ok());
    EXPECT_EQ(decoded.value().minMax, w.minMax);
    EXPECT_FALSE(media::decodeWaveform(bytes.substr(0, bytes.size() - 1)).ok());
    EXPECT_FALSE(media::decodeWaveform("UPWFxxxx").ok());
}

TEST(MediaAnalysis, WaveformGenerationCanBeCancelled) {
    test::TempDir dir;
    test::makeMedia(dir / "t.mp4", test::solid(0, 0, 0, 50));
    CancelToken token;
    token.cancel();
    auto r = media::generateWaveform(dir / "t.mp4", probe(dir / "t.mp4"), 48000, 480, &token);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::Cancelled);
}

TEST(MediaAnalysis, FingerprintChangesWithTheFile) {
    test::TempDir dir;
    std::ofstream(dir / "f.bin") << "one";
    const auto a = media::fileFingerprint(dir / "f.bin");
    ASSERT_TRUE(a.ok());
    std::ofstream(dir / "f.bin") << "longer contents";
    EXPECT_NE(media::fileFingerprint(dir / "f.bin").value(), a.value());
    EXPECT_EQ(media::fileFingerprint(dir / "missing.bin").error().code, ErrorCode::MediaOffline);
}

class MediaAssetsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test::makeMedia(dir / "red.mp4", test::solid(220, 20, 20, 25, 440));
        media::SyntheticSpec audio = test::solid(0, 0, 0, 25, 440);
        audio.video = false;
        test::makeMedia(dir / "a.m4a", audio);
        session = EditorSession::createNew("Assets");
        const auto ids = session->importMedia({dir / "red.mp4", dir / "a.m4a"}).importedIds;
        ASSERT_EQ(ids.size(), 2u);
        video = *session->project().findMedia(ids[0]);
        audioOnly = *session->project().findMedia(ids[1]);
    }

    test::TempDir dir;
    test::TempDir cacheDir;
    std::unique_ptr<EditorSession> session;
    MediaItem video;
    MediaItem audioOnly;
};

TEST_F(MediaAssetsTest, GeneratesInBackgroundThenServesFromMemoryAndDisk) {
    std::atomic<int> notifications{0};
    {
        MediaAssets assets(cacheDir.path());
        assets.setListener([&](const std::string&, MediaAssets::Kind) { ++notifications; });
        EXPECT_EQ(assets.thumbnail(video), nullptr);  // scheduled, not ready yet
        EXPECT_EQ(assets.waveform(video), nullptr);
        EXPECT_EQ(assets.thumbnail(audioOnly), nullptr);  // not applicable: never scheduled
        EXPECT_EQ(assets.waveform(audioOnly), nullptr);
        assets.waitIdle();
        EXPECT_EQ(notifications, 3);
        ASSERT_NE(assets.thumbnail(video), nullptr);
        EXPECT_GT(test::averageColor(*assets.thumbnail(video)).r, 180);
        ASSERT_NE(assets.waveform(video), nullptr);
        ASSERT_NE(assets.waveform(audioOnly), nullptr);
        EXPECT_EQ(assets.cache().entryCount(), 3u);
        EXPECT_EQ(assets.failedCount(), 0u);
    }
    // A new instance (e.g. next app start) loads from the disk cache without regenerating.
    const auto entry = DiskCache(cacheDir.path()).entryCount();
    MediaAssets again(cacheDir.path());
    (void)again.thumbnail(video);
    (void)again.waveform(video);
    again.waitIdle();
    EXPECT_NE(again.thumbnail(video), nullptr);
    EXPECT_EQ(DiskCache(cacheDir.path()).entryCount(), entry);
}

TEST_F(MediaAssetsTest, ChangedFileGetsFreshAssetsAndClearingRegenerates) {
    MediaAssets assets(cacheDir.path());
    (void)assets.thumbnail(video);
    assets.waitIdle();
    ASSERT_GT(test::averageColor(*assets.thumbnail(video)).r, 180);

    // Replace the file's contents (a new take rendered to the same path).
    test::makeMedia(dir / "red.mp4", test::solid(20, 20, 220, 30));
    fs::last_write_time(dir / "red.mp4", fs::last_write_time(dir / "red.mp4") + std::chrono::seconds(2));
    assets.reset();  // forget in-memory results (the UI does this via cache clear / reopen)
    (void)assets.thumbnail(video);
    assets.waitIdle();
    ASSERT_NE(assets.thumbnail(video), nullptr);
    EXPECT_GT(test::averageColor(*assets.thumbnail(video)).b, 180);
    EXPECT_EQ(assets.cache().entryCount(), 2u);  // old entry stays until evicted; harmless

    ASSERT_TRUE(assets.clearCache().ok());
    EXPECT_EQ(assets.cache().entryCount(), 0u);
    EXPECT_EQ(assets.thumbnail(video), nullptr);  // regenerated on demand
    assets.waitIdle();
    EXPECT_NE(assets.thumbnail(video), nullptr);
}

TEST_F(MediaAssetsTest, OfflineAndUndecodableMediaFailGracefully) {
    MediaAssets assets(cacheDir.path());
    MediaItem offline = video;
    offline.online = false;
    EXPECT_EQ(assets.thumbnail(offline), nullptr);

    MediaItem broken = video;
    broken.id = "broken";
    broken.path = dir / "broken.mp4";
    std::ofstream(broken.path) << "not a video";
    EXPECT_EQ(assets.thumbnail(broken), nullptr);
    assets.waitIdle();
    EXPECT_EQ(assets.thumbnail(broken), nullptr);  // failure is remembered, not retried in a loop
    EXPECT_EQ(assets.failedCount(), 1u);
}

TEST_F(MediaAssetsTest, DamagedCacheEntryIsRegenerated) {
    {
        MediaAssets assets(cacheDir.path());
        (void)assets.thumbnail(video);
        assets.waitIdle();
    }
    // Corrupt every cached file.
    for (auto& e : fs::recursive_directory_iterator(cacheDir.path()))
        if (e.is_regular_file()) std::ofstream(e.path(), std::ios::trunc) << "junk";
    MediaAssets assets(cacheDir.path());
    (void)assets.thumbnail(video);
    assets.waitIdle();
    ASSERT_NE(assets.thumbnail(video), nullptr);
    EXPECT_GT(test::averageColor(*assets.thumbnail(video)).r, 180);
}
