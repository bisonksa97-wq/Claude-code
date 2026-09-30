#include "codec/VideoDecoder.h"

#include <cmath>
#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "codec/FFmpegCommon.h"
#include "core/Log.h"

namespace up {

struct VideoDecoder::Impl {
    std::filesystem::path path;
    ffmpeg::FormatInputPtr format;
    ffmpeg::CodecContextPtr codec;
    ffmpeg::PacketPtr packet{av_packet_alloc()};
    ffmpeg::FramePtr current{av_frame_alloc()};    // last frame with pts <= requested time
    ffmpeg::FramePtr lookahead{av_frame_alloc()};  // first frame after `current`, when known
    bool hasCurrent = false;
    bool hasLookahead = false;
    bool eof = false;           // demuxer exhausted, decoder drained
    bool draining = false;      // flush packet already sent
    int streamIndex = -1;
    AVRational timeBase{1, 1};
    int64_t startPts = 0;
    // One scaler per output format (8-bit and 16-bit RGBA).
    struct Scaler {
        SwsContext* ctx = nullptr;
    };
    Scaler scaler8;
    Scaler scaler16;
    std::vector<uint8_t> scratch;  // padded conversion target

    // Decode forward beyond this many seconds triggers a seek instead.
    static constexpr double kForwardDecodeLimit = 2.0;

    ~Impl() {
        sws_freeContext(scaler8.ctx);
        sws_freeContext(scaler16.ctx);
    }

    int64_t toPts(double seconds) const {
        return startPts + static_cast<int64_t>(std::llround(seconds / av_q2d(timeBase)));
    }

    static int64_t ptsOf(const AVFrame* f) {
        return f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    }

    Error decodeError(const std::string& what, int err) const {
        return makeError(ErrorCode::DecodeError, "codec",
                         "Unable to decode '" + path.filename().string() + "' because " + what + ".",
                         "The file may be damaged. Try re-importing it or transcoding it to a standard format.",
                         ffmpeg::errorString(err));
    }

    // Decodes the next frame in presentation order into `out`. Returns false at end of stream.
    Result<bool> decodeNext(AVFrame* out) {
        while (true) {
            int err = avcodec_receive_frame(codec.get(), out);
            if (err == 0) return true;
            if (err == AVERROR_EOF) {
                eof = true;
                return false;
            }
            if (err != AVERROR(EAGAIN)) return decodeError("the decoder returned an invalid frame", err);
            if (draining) {
                eof = true;
                return false;
            }
            // Feed another packet from our stream.
            while (true) {
                err = av_read_frame(format.get(), packet.get());
                if (err == AVERROR_EOF) {
                    avcodec_send_packet(codec.get(), nullptr);
                    draining = true;
                    break;
                }
                if (err < 0) return decodeError("the file could not be read", err);
                if (packet->stream_index != streamIndex) {
                    av_packet_unref(packet.get());
                    continue;
                }
                err = avcodec_send_packet(codec.get(), packet.get());
                av_packet_unref(packet.get());
                if (err < 0 && err != AVERROR(EAGAIN) && err != AVERROR_INVALIDDATA) {
                    return decodeError("a compressed packet was rejected", err);
                }
                break;
            }
        }
    }

    Status seek(int64_t targetPts) {
        const int err = av_seek_frame(format.get(), streamIndex, targetPts, AVSEEK_FLAG_BACKWARD);
        if (err < 0) {
            // Some formats cannot seek by timestamp; restart from the beginning instead.
            const int restart = avformat_seek_file(format.get(), streamIndex, INT64_MIN, 0, 0, 0);
            if (restart < 0) return decodeError("seeking failed", err);
        }
        avcodec_flush_buffers(codec.get());
        hasCurrent = hasLookahead = eof = draining = false;
        return Status::success();
    }

    // Positions `current` on the frame displayed at targetPts.
    Status positionAt(int64_t targetPts) {
        const int64_t forwardLimit = static_cast<int64_t>(kForwardDecodeLimit / av_q2d(timeBase));
        if (hasCurrent && targetPts >= ptsOf(current.get())) {
            if (hasLookahead && targetPts < ptsOf(lookahead.get())) return Status::success();
            if (!hasLookahead && eof) return Status::success();  // hold last frame
            if (targetPts - ptsOf(current.get()) > forwardLimit) UP_TRY(seek(targetPts));
        } else {
            UP_TRY(seek(targetPts));
        }

        while (true) {
            if (hasLookahead) {
                if (ptsOf(lookahead.get()) > targetPts && hasCurrent) return Status::success();
                std::swap(current, lookahead);
                hasCurrent = true;
                hasLookahead = false;
                if (ptsOf(current.get()) > targetPts) return Status::success();  // target precedes first frame
                continue;
            }
            auto got = decodeNext(lookahead.get());
            if (!got.ok()) return got.error();
            if (!got.value()) {
                if (hasCurrent) return Status::success();  // past the end: hold the last frame
                return makeError(ErrorCode::DecodeError, "codec",
                                 "'" + path.filename().string() + "' contains no decodable video frames.",
                                 "The file may be damaged or use an unsupported codec.");
            }
            hasLookahead = true;
        }
    }

    // Converts to packed RGBA of `Frame`'s component type (8-bit RGBA or native-endian RGBA64).
    template <typename Frame>
    Result<Frame> convert(const AVFrame* src, int outW, int outH, AVPixelFormat outFormat, Scaler& scaler) {
        if (outW <= 0 || outH <= 0) {
            outW = src->width;
            outH = src->height;
        }
        SwsContext*& sws = scaler.ctx;
        sws = sws_getCachedContext(sws, src->width, src->height, static_cast<AVPixelFormat>(src->format), outW, outH,
                                   outFormat, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) {
            return makeError(ErrorCode::DecodeError, "codec", "Unable to convert the decoded frame to RGB.",
                             "The pixel format may be unsupported.");
        }
        // Use the stream's YUV matrix and range (swscale otherwise assumes BT.601 limited).
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(src->format));
        if (desc && !(desc->flags & AV_PIX_FMT_FLAG_RGB)) {
            const int matrix = ffmpeg::swsMatrixFor(src->colorspace, src->height);
            const std::string name = desc->name ? desc->name : "";
            const int fullRange = src->color_range == AVCOL_RANGE_JPEG || name.rfind("yuvj", 0) == 0 ? 1 : 0;
            ffmpeg::ensureSwsColorspace(sws, matrix, fullRange, 1);
        }
        // Convert into padded scratch memory (see ffmpeg::alignedStride), then copy the
        // tightly packed rows out, so the returned frame never has to absorb SIMD overshoot.
        constexpr int bytesPerPixel = 4 * static_cast<int>(sizeof(typename decltype(Frame::pixels)::value_type));
        const int stride = ffmpeg::alignedStride(outW * bytesPerPixel);
        scratch.resize(static_cast<std::size_t>(stride) * outH + ffmpeg::kSwsAlign);
        uint8_t* dst[4] = {scratch.data(), nullptr, nullptr, nullptr};
        int dstStride[4] = {stride, 0, 0, 0};
        sws_scale(sws, src->data, src->linesize, 0, src->height, dst, dstStride);
        Frame out(outW, outH);
        for (int y = 0; y < outH; ++y)
            std::memcpy(out.row(y), scratch.data() + static_cast<std::size_t>(y) * stride,
                        static_cast<std::size_t>(outW) * bytesPerPixel);
        return out;
    }
};

VideoDecoder::VideoDecoder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
VideoDecoder::~VideoDecoder() = default;

Result<std::unique_ptr<VideoDecoder>> VideoDecoder::open(const std::filesystem::path& path) {
    ffmpeg::initialize();
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    AVFormatContext* raw = nullptr;
    int err = avformat_open_input(&raw, path.string().c_str(), nullptr, nullptr);
    if (err < 0) {
        return makeError(ErrorCode::MediaOffline, "codec", "Unable to open '" + path.string() + "'.",
                         "Check that the file exists or relink the media.", ffmpeg::errorString(err));
    }
    impl->format.reset(raw);
    if ((err = avformat_find_stream_info(raw, nullptr)) < 0) return impl->decodeError("stream info is unreadable", err);

    const AVCodec* decoder = nullptr;
    impl->streamIndex = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (impl->streamIndex < 0 || !decoder) {
        return makeError(ErrorCode::DecodeError, "codec", "'" + path.filename().string() + "' has no video stream.",
                         "Use this file on an audio track instead.");
    }
    AVStream* stream = raw->streams[impl->streamIndex];
    impl->timeBase = stream->time_base;
    impl->startPts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    impl->codec.reset(avcodec_alloc_context3(decoder));
    avcodec_parameters_to_context(impl->codec.get(), stream->codecpar);
    impl->codec->thread_count = 0;  // let FFmpeg choose
    if ((err = avcodec_open2(impl->codec.get(), decoder, nullptr)) < 0) {
        return impl->decodeError(std::string("the ") + decoder->name + " decoder could not be opened", err);
    }
    UP_LOG_DEBUG(log::sub::Codec, "Opened video decoder " << decoder->name << " for " << path.string());
    return std::unique_ptr<VideoDecoder>(new VideoDecoder(std::move(impl)));
}

int VideoDecoder::width() const { return impl_->codec->width; }
int VideoDecoder::height() const { return impl_->codec->height; }

Result<VideoFrame> VideoDecoder::frameAt(double seconds, int outWidth, int outHeight) {
    UP_TRY(impl_->positionAt(impl_->toPts(std::max(0.0, seconds))));
    return impl_->convert<VideoFrame>(impl_->current.get(), outWidth, outHeight, AV_PIX_FMT_RGBA, impl_->scaler8);
}

Result<VideoFrame16> VideoDecoder::frameAt16(double seconds, int outWidth, int outHeight) {
    UP_TRY(impl_->positionAt(impl_->toPts(std::max(0.0, seconds))));
    return impl_->convert<VideoFrame16>(impl_->current.get(), outWidth, outHeight, AV_PIX_FMT_RGBA64, impl_->scaler16);
}

}  // namespace up
