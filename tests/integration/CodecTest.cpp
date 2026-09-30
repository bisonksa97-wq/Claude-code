#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <set>
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
std::array<int, 3> frameYuv(const std::filesystem::path& path, int index = 0) {
    AVFormatContext* fmt = nullptr;
    EXPECT_GE(avformat_open_input(&fmt, path.string().c_str(), nullptr, nullptr), 0);
    avformat_find_stream_info(fmt, nullptr);
    const int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[stream]->codecpar);
    avcodec_open2(ctx, codec, nullptr);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::array<int, 3> yuv{-1, -1, -1};
    bool done = false;
    int seen = 0;
    auto receive = [&] {
        while (!done && avcodec_receive_frame(ctx, frame) >= 0) {
            if (seen++ == index) done = true;
            else av_frame_unref(frame);
        }
    };
    while (!done && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream && avcodec_send_packet(ctx, packet) >= 0) receive();
        av_packet_unref(packet);
    }
    if (!done) {
        avcodec_send_packet(ctx, nullptr);
        receive();
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
        for (int i = 0; i < 12; ++i) EXPECT_TRUE(writer.value()->writeVideo(red).ok());
        EXPECT_TRUE(writer.value()->finish().ok());
        return dir / name;
    };
    // BT.709 limited range: red = Y 63, Cb 102, Cr 240 (BT.601 would give Y 81, Cb 90).
    const auto hd = encode("709.mp4", "bt709", "bt709", "bt709");
    const auto yuv709 = frameYuv(hd);
    EXPECT_NEAR(yuv709[0], 63, 2);
    EXPECT_NEAR(yuv709[1], 102, 2);
    EXPECT_NEAR(yuv709[2], 240, 2);
    // Every frame, not just the first: swscale may recreate its context between frames,
    // which once silently reset later frames to BT.601 (Y 81) on both sides.
    for (int n : {1, 4, 11}) {
        const auto later = frameYuv(hd, n);
        EXPECT_NEAR(later[0], 63, 2) << "frame " << n;
        EXPECT_NEAR(later[1], 102, 2) << "frame " << n;
    }
    {
        auto decoder = VideoDecoder::open(hd);
        ASSERT_TRUE(decoder.ok());
        for (double t : {0.0, 0.1, 0.2, 0.3, 0.44}) {
            const auto rgb = test::averageColor(decoder.value()->frameAt(t).value());
            EXPECT_NEAR(rgb.r, 255, 4) << "at " << t;
            EXPECT_NEAR(rgb.g, 0, 4) << "at " << t;
        }
    }
    const auto sd = encode("601.mp4", "smpte170m", "smpte170m", "smpte170m");
    const auto yuv601 = frameYuv(sd);
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

namespace {

// A horizontal ramp over a narrow band (0.40..0.44 of full scale): 8 bits can only
// show ~11 distinct levels of it, 10 bits ~41, 16 bits every column.
VideoFrame16 narrowRamp(int width, int height) {
    VideoFrame16 f(width, height);
    for (int y = 0; y < height; ++y) {
        uint16_t* row = f.row(y);
        for (int x = 0; x < width; ++x) {
            const auto v = static_cast<uint16_t>(std::lround((0.40 + 0.04 * x / (width - 1)) * 65535.0));
            row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = v;
            row[x * 4 + 3] = 65535;
        }
    }
    return f;
}

std::size_t distinctLevels(const VideoFrame16& f, int shift) {
    std::set<int> levels;
    for (int x = 0; x < f.width; ++x) levels.insert(f.row(f.height / 2)[x * 4] >> shift);
    return levels.size();
}

EncodeSettings videoOnly(int w, int h, const std::string& codec, const std::string& pixelFormat) {
    EncodeSettings s;
    s.width = w;
    s.height = h;
    s.audio = false;
    s.videoCodec = codec;
    s.pixelFormat = pixelFormat;
    return s;
}

}  // namespace

TEST(CodecDepth, TenBitLosslessRoundTrip) {
    if (!encoderAvailable("ffv1")) GTEST_SKIP() << "ffv1 encoder not available";
    test::TempDir dir;
    const VideoFrame16 ramp = narrowRamp(512, 16);
    auto writer = MediaWriter::open(dir / "ramp.mkv", videoOnly(512, 16, "ffv1", "gbrp10le"));
    ASSERT_TRUE(writer.ok()) << writer.error().toString();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(writer.value()->writeVideo(ramp).ok());
    ASSERT_TRUE(writer.value()->finish().ok());
    auto info = probeMedia(dir / "ramp.mkv");
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().bitDepth, 10);
    auto decoder = VideoDecoder::open(dir / "ramp.mkv");
    ASSERT_TRUE(decoder.ok());
    const VideoFrame16 deep = decoder.value()->frameAt16(0.0).value();
    const VideoFrame shallow = decoder.value()->frameAt(0.0).value();
    // Every value comes back within one 10-bit step (swscale truncates between 16 and
    // 10 bits rather than rounding), and the ramp keeps ~41 levels.
    for (int x = 0; x < 512; ++x) EXPECT_NEAR(deep.row(8)[x * 4], ramp.row(8)[x * 4], 65535.0 / 1023 + 1);
    EXPECT_GE(distinctLevels(deep, 6), 40u);
    std::set<int> eightBit;
    for (int x = 0; x < 512; ++x) eightBit.insert(shallow.row(8)[x * 4]);
    EXPECT_LE(eightBit.size(), 12u);
}

TEST(CodecDepth, HevcMain10WithHdr10Metadata) {
    if (!encoderAvailable("libx265")) GTEST_SKIP() << "libx265 not available";
    test::TempDir dir;
    EncodeSettings s = videoOnly(128, 64, "libx265", "yuv420p10le");
    s.colorPrimaries = "bt2020";
    s.colorTransfer = "smpte2084";
    s.colorMatrix = "bt2020nc";
    s.codecTag = "hvc1";
    s.crf = 12;
    HdrMetadata hdr;
    hdr.maxLuminance = 1000;
    hdr.minLuminance = 0.005;
    hdr.maxCll = 800;
    hdr.maxFall = 300;
    s.hdr = hdr;
    auto writer = MediaWriter::open(dir / "hdr.mp4", s);
    ASSERT_TRUE(writer.ok()) << writer.error().toString();
    const VideoFrame16 ramp = narrowRamp(128, 64);
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(writer.value()->writeVideo(ramp).ok());
    ASSERT_TRUE(writer.value()->finish().ok());
    auto info = probeMedia(dir / "hdr.mp4");
    ASSERT_TRUE(info.ok());
    EXPECT_EQ(info.value().videoCodec, "hevc");
    EXPECT_EQ(info.value().bitDepth, 10);
    EXPECT_EQ(info.value().pixelFormat, "yuv420p10le");
    EXPECT_EQ(info.value().colorTransfer, "smpte2084");
    EXPECT_NEAR(info.value().masteringMaxLuminance, 1000, 1e-6);
    EXPECT_NEAR(info.value().masteringMinLuminance, 0.005, 1e-6);
    EXPECT_EQ(info.value().maxCll, 800);
    EXPECT_EQ(info.value().maxFall, 300);
    auto decoder = VideoDecoder::open(dir / "hdr.mp4");
    ASSERT_TRUE(decoder.ok());
    EXPECT_GE(distinctLevels(decoder.value()->frameAt16(0.0).value(), 6), 25u);  // lossy, but well beyond 8 bits
}

TEST(CodecDepth, ProResPcmWavAndImageSequences) {
    test::TempDir dir;
    VideoFrame red(64, 32);
    red.fill(200, 40, 40);
    if (encoderAvailable("prores_ks")) {
        EncodeSettings s = videoOnly(64, 32, "prores_ks", "yuv422p10le");
        s.codecOptions["profile"] = "3";  // 422 HQ
        s.audio = true;
        s.audioCodec = "pcm_s24le";
        auto writer = MediaWriter::open(dir / "master.mov", s);
        ASSERT_TRUE(writer.ok()) << writer.error().toString();
        std::vector<float> tone(1920 * 2);
        for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * static_cast<float>(std::sin(static_cast<double>(i / 2) * 0.05));
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(writer.value()->writeVideo(red).ok());
            ASSERT_TRUE(writer.value()->writeAudio(tone.data(), 1920).ok());
        }
        ASSERT_TRUE(writer.value()->finish().ok());
        auto info = probeMedia(dir / "master.mov");
        ASSERT_TRUE(info.ok());
        EXPECT_EQ(info.value().videoCodec, "prores");
        EXPECT_EQ(info.value().pixelFormat, "yuv422p10le");
        EXPECT_EQ(info.value().audioCodec, "pcm_s24le");
        const auto rgb = test::averageColor(VideoDecoder::open(dir / "master.mov").value()->frameAt(0.0).value());
        EXPECT_NEAR(rgb.r, 200, 3);
        EXPECT_NEAR(rgb.g, 40, 3);
    }
    // Audio-only WAV at 24 bits: the samples survive.
    EncodeSettings wav;
    wav.video = false;
    wav.audioCodec = "pcm_s24le";
    auto writer = MediaWriter::open(dir / "mix.wav", wav);
    ASSERT_TRUE(writer.ok()) << writer.error().toString();
    std::vector<float> ramp(4800 * 2);
    for (std::size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i % 200) / 200.0f - 0.5f;
    ASSERT_TRUE(writer.value()->writeAudio(ramp.data(), 4800).ok());
    ASSERT_TRUE(writer.value()->finish().ok());
    auto audio = AudioDecoder::open(dir / "mix.wav", 48000, 2);
    ASSERT_TRUE(audio.ok());
    std::vector<float> back(4800 * 2);
    ASSERT_TRUE(audio.value()->read(0, 4800, back.data()).ok());
    for (std::size_t i = 0; i < 2000; ++i) EXPECT_NEAR(back[i], ramp[i], 1e-5);
    EXPECT_EQ(probeMedia(dir / "mix.wav").value().audioCodec, "pcm_s24le");
    // PNG sequence (16-bit RGB): one file per frame.
    std::filesystem::create_directories(dir / "seq");
    auto png = MediaWriter::open(dir / "seq" / "frame_%06d.png", videoOnly(64, 32, "png", "rgb48be"));
    ASSERT_TRUE(png.ok()) << png.error().toString();
    const VideoFrame16 deep = narrowRamp(64, 32);
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(png.value()->writeVideo(deep).ok());
    ASSERT_TRUE(png.value()->finish().ok());
    EXPECT_TRUE(std::filesystem::exists(dir / "seq" / "frame_000001.png"));
    EXPECT_TRUE(std::filesystem::exists(dir / "seq" / "frame_000003.png"));
    auto still = probeMedia(dir / "seq" / "frame_000002.png");
    ASSERT_TRUE(still.ok());
    EXPECT_EQ(still.value().bitDepth, 16);
    // Pixel formats the encoder cannot write are refused clearly.
    auto bad = MediaWriter::open(dir / "bad.mp4", videoOnly(64, 32, "libx264", "yuv422p16le"));
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("cannot write"), std::string::npos);
}
