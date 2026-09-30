#pragma once

// Internal helpers shared by the FFmpeg-backed codec implementation.
// Not part of the public codec API: nothing outside src/codec includes this.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <memory>
#include <string>

namespace up::ffmpeg {

inline std::string errorString(int err) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buffer, sizeof(buffer));
    return buffer;
}

struct FormatInputDeleter {
    void operator()(AVFormatContext* ctx) const { avformat_close_input(&ctx); }
};
struct CodecContextDeleter {
    void operator()(AVCodecContext* ctx) const { avcodec_free_context(&ctx); }
};
struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};

using FormatInputPtr = std::unique_ptr<AVFormatContext, FormatInputDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

// Makes FFmpeg's own logging quiet unless explicitly enabled.
void initialize();

// swscale's SIMD paths read and write whole vector blocks, so a row may be touched
// past its last pixel. Buffers handed to sws_scale therefore use strides aligned to
// this many bytes plus the same amount of trailing slack.
inline constexpr int kSwsAlign = 64;
inline int alignedStride(int bytes) { return (bytes + kSwsAlign - 1) / kSwsAlign * kSwsAlign; }

// swscale colourspace (SWS_CS_*) for a stream's YUV matrix. Unspecified matrices
// follow common practice: BT.709 for HD and larger, BT.601 below 720 lines.
int swsMatrixFor(AVColorSpace space, int height);
// swscale colourspace for an FFmpeg matrix name ("bt709", "bt2020nc", ...).
int swsMatrixForName(const std::string& name);

}  // namespace up::ffmpeg
