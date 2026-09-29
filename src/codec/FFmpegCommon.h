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

}  // namespace up::ffmpeg
