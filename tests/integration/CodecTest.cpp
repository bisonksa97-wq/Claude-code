#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "codec/AudioDecoder.h"
#include "codec/MediaProbe.h"
#include "codec/MediaWriter.h"
#include "codec/VideoDecoder.h"
#include "support/TestSupport.h"

using namespace up;

namespace {

// 64 frames whose grey level is frame * 4, so every decoded frame identifies itself.
class CodecTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        dir = new test::TempDir();
        media::SyntheticSpec spec;
        spec.pattern = media::SyntheticSpec::Pattern::FrameRamp;
        spec.rampStep = 4;
        spec.frames = 64;
        spec.width = 128;
        spec.height = 96;
        spec.toneHz = 1000;
        spec.toneLevel = 0.5f;
        ASSERT_TRUE(media::generateSyntheticMedia(*dir / "ramp.mp4", spec).ok());
    }
    static void TearDownTestSuite() {
        delete dir;
        dir = nullptr;
    }

    static int frameNumberOf(const VideoFrame& f) {
        return static_cast<int>(std::lround(test::averageColor(f).g / 4.0));
    }

    static test::TempDir* dir;
};

test::TempDir* CodecTest::dir = nullptr;

}  // namespace

TEST_F(CodecTest, ProbeReportsStreams) {
    auto info = probeMedia(*dir / "ramp.mp4");
    ASSERT_TRUE(info.ok()) << info.error().toString();
    EXPECT_TRUE(info.value().hasVideo);
    EXPECT_EQ(info.value().width, 128);
    EXPECT_EQ(info.value().height, 96);
    EXPECT_EQ(info.value().frameRate, (FrameRate{25, 1}));
    EXPECT_NEAR(info.value().durationSeconds, 64 / 25.0, 0.05);
    EXPECT_TRUE(info.value().hasAudio);
    EXPECT_EQ(info.value().sampleRate, 48000);
    EXPECT_EQ(info.value().channels, 2);
}

TEST_F(CodecTest, ProbeGivesReadableErrors) {
    auto missing = probeMedia(*dir / "nope.mp4");
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
    std::ofstream(*dir / "junk.mp4") << "definitely not a video";
    auto junk = probeMedia(*dir / "junk.mp4");
    ASSERT_FALSE(junk.ok());
    EXPECT_FALSE(junk.error().suggestion.empty());
}

TEST_F(CodecTest, SequentialDecodeIsFrameAccurate) {
    auto dec = VideoDecoder::open(*dir / "ramp.mp4");
    ASSERT_TRUE(dec.ok());
    for (int f = 0; f < 64; ++f) {
        auto frame = dec.value()->frameAt(f / 25.0 + 0.001);
        ASSERT_TRUE(frame.ok());
        EXPECT_EQ(frameNumberOf(frame.value()), f);
    }
}

TEST_F(CodecTest, RandomAccessDecodeIsFrameAccurate) {
    auto dec = VideoDecoder::open(*dir / "ramp.mp4");
    ASSERT_TRUE(dec.ok());
    for (int f : {40, 3, 63, 0, 17, 18, 16, 55, 1, 62}) {
        auto frame = dec.value()->frameAt(f / 25.0 + 0.001);
        ASSERT_TRUE(frame.ok());
        EXPECT_EQ(frameNumberOf(frame.value()), f) << "requested " << f;
    }
}

TEST_F(CodecTest, DecodePastEndHoldsLastFrameAndScales) {
    auto dec = VideoDecoder::open(*dir / "ramp.mp4");
    ASSERT_TRUE(dec.ok());
    auto frame = dec.value()->frameAt(100.0, 64, 48);
    ASSERT_TRUE(frame.ok());
    EXPECT_EQ(frame.value().width, 64);
    EXPECT_EQ(frame.value().height, 48);
    EXPECT_EQ(frameNumberOf(frame.value()), 63);
}

// Regression: swscale's SIMD code overruns rows that are not a multiple of 64 bytes.
// Converting through padded scratch memory must keep arbitrary sizes safe.
TEST_F(CodecTest, ConvertsToAndFromAwkwardSizes) {
    auto dec = VideoDecoder::open(*dir / "ramp.mp4");
    ASSERT_TRUE(dec.ok());
    for (auto [w, h] : {std::pair{120, 120}, {118, 77}, {33, 17}, {2, 2}, {250, 3}}) {
        for (int f : {5, 6, 40}) {
            auto frame = dec.value()->frameAt(f / 25.0 + 0.001, w, h);
            ASSERT_TRUE(frame.ok());
            EXPECT_EQ(frame.value().width, w);
            EXPECT_EQ(frame.value().pixels.size(), static_cast<std::size_t>(w) * h * 4);
            EXPECT_NEAR(test::averageColor(frame.value()).g, f * 4, 6) << w << "x" << h;  // extreme downscales blur a little
        }
    }
    media::SyntheticSpec spec = test::solid(10, 200, 10, 5);
    spec.width = 122;  // 488-byte RGBA rows
    spec.height = 78;
    test::makeMedia(*dir / "odd.mp4", spec);
    auto info = probeMedia(*dir / "odd.mp4");
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().width, 122);
}

TEST_F(CodecTest, AudioDecodeReproducesTone) {
    auto dec = AudioDecoder::open(*dir / "ramp.mp4", 48000, 2);
    ASSERT_TRUE(dec.ok());
    std::vector<float> buf(4800 * 2);
    // Read from the middle (avoids encoder priming at the start), then out of order.
    for (int64_t start : {24000, 48000, 12000}) {
        ASSERT_TRUE(dec.value()->read(start, 4800, buf.data()).ok());
        double sumSq = 0;
        for (float v : buf) sumSq += static_cast<double>(v) * v;
        const double rms = std::sqrt(sumSq / static_cast<double>(buf.size()));
        EXPECT_NEAR(rms, 0.5 / std::sqrt(2.0), 0.05) << "at " << start;
    }
    // Beyond the end is silence.
    ASSERT_TRUE(dec.value()->read(48000 * 10, 100, buf.data()).ok());
    EXPECT_FLOAT_EQ(buf[0], 0.0f);
}

TEST_F(CodecTest, AudioDecodeIsSampleAligned) {
    // Compare a sequential read against a random-access read of the same range.
    auto a = AudioDecoder::open(*dir / "ramp.mp4", 48000, 2);
    auto b = AudioDecoder::open(*dir / "ramp.mp4", 48000, 2);
    ASSERT_TRUE(a.ok() && b.ok());
    std::vector<float> seq(2000 * 2), rnd(2000 * 2);
    for (int64_t s = 0; s < 60000; s += 2000) ASSERT_TRUE(a.value()->read(s, 2000, seq.data()).ok());
    ASSERT_TRUE(a.value()->read(60000, 2000, seq.data()).ok());
    ASSERT_TRUE(b.value()->read(60000, 2000, rnd.data()).ok());
    double diff = 0;
    for (std::size_t i = 0; i < seq.size(); ++i) diff = std::max(diff, static_cast<double>(std::abs(seq[i] - rnd[i])));
    EXPECT_LT(diff, 0.005);
}

TEST_F(CodecTest, WriterFallsBackAndValidates) {
    EXPECT_FALSE(defaultVideoEncoder().empty());
    EncodeSettings bad;
    bad.width = 101;
    EXPECT_EQ(MediaWriter::open(*dir / "bad.mp4", bad).error().code, ErrorCode::InvalidArgument);
    EncodeSettings unknown;
    unknown.videoCodec = "no-such-encoder";
    EXPECT_EQ(MediaWriter::open(*dir / "bad.mp4", unknown).error().code, ErrorCode::NotFound);

    // MPEG-4 Part 2 is built into FFmpeg, so it is always available as a fallback.
    media::SyntheticSpec spec = test::solid(10, 200, 10, 10);
    spec.videoCodec = "mpeg4";
    test::makeMedia(*dir / "mpeg4.mp4", spec);
    auto info = probeMedia(*dir / "mpeg4.mp4");
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().videoCodec, "mpeg4");
}

namespace {

// Y, Cb, Cr at the centre of the first decoded frame, read straight from libav (no
// RGB conversion), so the encoder's matrix is checked independently of our decoder.
std::array<int, 3> firstFrameYuv(const std::filesystem::path& path) {
    AVFormatContext* fmt = nullptr;
    EXPECT_GE(avformat_open_input(&fmt, path.string().c_str(), nullptr, nullptr), 0);
    avformat_find_stream_info(fmt, nullptr);
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[index]->codecpar);
    avcodec_open2(ctx, codec, nullptr);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::array<int, 3> yuv{-1, -1, -1};
    bool done = false;
    while (!done && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == index && avcodec_send_packet(ctx, packet) >= 0 && avcodec_receive_frame(ctx, frame) >= 0) done = true;
        av_packet_unref(packet);
    }
    if (!done) {
        avcodec_send_packet(ctx, nullptr);
        done = avcodec_receive_frame(ctx, frame) >= 0;
    }
    if (done) {
        const int x = frame->width / 2, y = frame->height / 2;
        yuv = {frame->data[0][y * frame->linesize[0] + x], frame->data[1][(y / 2) * frame->linesize[1] + x / 2],
               frame->data[2][(y / 2) * frame->linesize[2] + x / 2]};
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return yuv;
}

}  // namespace

TEST(CodecColor, YuvMatrixAndTagsFollowTheSettings) {
    test::TempDir dir;
    VideoFrame red(64, 64);
    red.fill(255, 0, 0);
    auto encode = [&](const std::string& name, const std::string& matrix, const std::string& primaries, const std::string& transfer) {
        EncodeSettings s;
        s.width = 64;
        s.height = 64;
        s.audio = false;
        s.colorMatrix = matrix;
        s.colorPrimaries = primaries;
        s.colorTransfer = transfer;
        auto writer = MediaWriter::open(dir / name, s);
        EXPECT_TRUE(writer.ok());
        for (int i = 0; i < 5; ++i) EXPECT_TRUE(writer.value()->writeVideo(red).ok());
        EXPECT_TRUE(writer.value()->finish().ok());
        return dir / name;
    };
    // BT.709 limited range: red = Y 63, Cb 102, Cr 240 (BT.601 would give Y 81, Cb 90).
    const auto hd = encode("709.mp4", "bt709", "bt709", "bt709");
    const auto yuv709 = firstFrameYuv(hd);
    EXPECT_NEAR(yuv709[0], 63, 2);
    EXPECT_NEAR(yuv709[1], 102, 2);
    EXPECT_NEAR(yuv709[2], 240, 2);
    const auto sd = encode("601.mp4", "smpte170m", "smpte170m", "smpte170m");
    const auto yuv601 = firstFrameYuv(sd);
    EXPECT_NEAR(yuv601[0], 81, 2);
    EXPECT_NEAR(yuv601[1], 90, 2);
    // Tags are written, and our decoder uses them to get the RGB back either way.
    for (const auto& file : {hd, sd}) {
        auto info = probeMedia(file);
        ASSERT_TRUE(info.ok());
        EXPECT_EQ(info.value().colorRange, "tv");
        auto decoder = VideoDecoder::open(file);
        ASSERT_TRUE(decoder.ok());
        const auto rgb = test::averageColor(decoder.value()->frameAt(0.0).value());
        EXPECT_NEAR(rgb.r, 255, 4);
        EXPECT_NEAR(rgb.g, 0, 4);
        EXPECT_NEAR(rgb.b, 0, 4);
    }
    EXPECT_EQ(probeMedia(hd).value().colorMatrix, "bt709");
    EXPECT_EQ(probeMedia(sd).value().colorMatrix, "smpte170m");
    EncodeSettings bad;
    bad.width = 64;
    bad.height = 64;
    bad.audio = false;
    bad.colorTransfer = "not-a-transfer";
    EXPECT_FALSE(MediaWriter::open(dir / "bad.mp4", bad).ok());
}
