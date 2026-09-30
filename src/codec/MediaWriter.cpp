#include "codec/MediaWriter.h"

#include <algorithm>
#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include "codec/FFmpegCommon.h"
#include "core/Log.h"

namespace up {

bool encoderAvailable(const std::string& name) {
    return avcodec_find_encoder_by_name(name.c_str()) != nullptr;
}

std::string defaultVideoEncoder() {
    for (const char* name : {"libx264", "h264_videotoolbox", "h264_mf", "libopenh264", "mpeg4"})
        if (encoderAvailable(name)) return name;
    return "mpeg4";
}

struct MediaWriter::Impl {
    std::filesystem::path path;
    EncodeSettings settings;
    AVFormatContext* format = nullptr;
    ffmpeg::CodecContextPtr video;
    ffmpeg::CodecContextPtr audio;
    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    ffmpeg::FramePtr videoFrame{av_frame_alloc()};
    ffmpeg::FramePtr audioFrame{av_frame_alloc()};
    ffmpeg::PacketPtr packet{av_packet_alloc()};
    SwsContext* sws = nullptr;
    const SwsContext* configuredSws = nullptr;  // sws with the YUV matrix set
    std::vector<uint8_t> scratch;  // padded RGBA copy for swscale
    int64_t videoPts = 0;
    int64_t audioPts = 0;
    std::vector<float> audioFifo;  // interleaved samples waiting for a full encoder frame
    bool headerWritten = false;
    bool finished = false;

    ~Impl() {
        sws_freeContext(sws);
        if (format) {
            if (!(format->oformat->flags & AVFMT_NOFILE) && format->pb) avio_closep(&format->pb);
            avformat_free_context(format);
        }
    }

    Error encodeError(const std::string& what, int err) const {
        return makeError(ErrorCode::EncodeError, "codec",
                         "Unable to write '" + path.filename().string() + "' because " + what + ".",
                         "Check the export settings, free disk space and write permissions.", ffmpeg::errorString(err));
    }

    Status drain(AVCodecContext* ctx, AVStream* stream) {
        while (true) {
            int err = avcodec_receive_packet(ctx, packet.get());
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) return Status::success();
            if (err < 0) return encodeError("the encoder failed", err);
            av_packet_rescale_ts(packet.get(), ctx->time_base, stream->time_base);
            packet->stream_index = stream->index;
            err = av_interleaved_write_frame(format, packet.get());
            if (err < 0) return encodeError("writing to the file failed", err);
        }
    }

    Status send(AVCodecContext* ctx, AVStream* stream, AVFrame* frame) {
        const int err = avcodec_send_frame(ctx, frame);
        if (err < 0 && err != AVERROR_EOF) return encodeError("the encoder rejected a frame", err);
        return drain(ctx, stream);
    }

    Status encodeAudioChunk(int samples) {
        av_frame_unref(audioFrame.get());
        audioFrame->format = audio->sample_fmt;
        audioFrame->nb_samples = samples;
        audioFrame->sample_rate = audio->sample_rate;
        av_channel_layout_copy(&audioFrame->ch_layout, &audio->ch_layout);
        int err = av_frame_get_buffer(audioFrame.get(), 0);
        if (err < 0) return encodeError("audio buffers could not be allocated", err);
        const int ch = settings.channels;
        // AAC and most encoders take planar float; interleaved float is handled too.
        if (audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
            for (int c = 0; c < ch; ++c) {
                auto* plane = reinterpret_cast<float*>(audioFrame->extended_data[c]);
                for (int i = 0; i < samples; ++i) plane[i] = std::clamp(audioFifo[static_cast<std::size_t>(i * ch + c)], -1.0f, 1.0f);
            }
        } else {
            auto* dst = reinterpret_cast<float*>(audioFrame->data[0]);
            for (int i = 0; i < samples * ch; ++i) dst[i] = std::clamp(audioFifo[static_cast<std::size_t>(i)], -1.0f, 1.0f);
        }
        audioFifo.erase(audioFifo.begin(), audioFifo.begin() + static_cast<std::ptrdiff_t>(samples) * ch);
        audioFrame->pts = audioPts;
        audioPts += samples;
        return send(audio.get(), audioStream, audioFrame.get());
    }
};

MediaWriter::MediaWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

MediaWriter::~MediaWriter() {
    if (impl_ && impl_->headerWritten && !impl_->finished) {
        UP_LOG_WARN(log::sub::Codec, "MediaWriter destroyed without finish(); output may be incomplete: "
                                         << impl_->path.string());
    }
}

const EncodeSettings& MediaWriter::settings() const { return impl_->settings; }

Result<std::unique_ptr<MediaWriter>> MediaWriter::open(const std::filesystem::path& path, const EncodeSettings& in) {
    ffmpeg::initialize();
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->settings = in;
    EncodeSettings& s = impl->settings;
    if (s.videoCodec.empty()) s.videoCodec = defaultVideoEncoder();
    if (!s.video && !s.audio) {
        return makeError(ErrorCode::InvalidArgument, "codec", "An export needs at least a video or an audio stream.");
    }
    if (s.video && (s.width <= 0 || s.height <= 0 || s.width % 2 || s.height % 2 || !s.frameRate.valid())) {
        return makeError(ErrorCode::InvalidArgument, "codec",
                         "The export size must be a positive even width and height with a valid frame rate.",
                         "Adjust the timeline resolution (e.g. 1920x1080) and try again.",
                         std::to_string(s.width) + "x" + std::to_string(s.height) + " @ " + s.frameRate.toString());
    }

    int err = avformat_alloc_output_context2(&impl->format, nullptr, nullptr, path.string().c_str());
    if (err < 0 || !impl->format) {
        return makeError(ErrorCode::InvalidArgument, "codec",
                         "Unable to choose an output format for '" + path.filename().string() + "'.",
                         "Use a file extension such as .mp4, .mov or .mkv.", ffmpeg::errorString(err));
    }

    if (s.video) {
        const AVCodec* vcodec = avcodec_find_encoder_by_name(s.videoCodec.c_str());
        if (!vcodec) {
            return makeError(ErrorCode::NotFound, "codec", "The video encoder '" + s.videoCodec + "' is not available.",
                             "Choose a different codec; '" + defaultVideoEncoder() + "' is available on this system.");
        }
        impl->videoStream = avformat_new_stream(impl->format, nullptr);
        impl->video.reset(avcodec_alloc_context3(vcodec));
        AVCodecContext* v = impl->video.get();
        v->width = s.width;
        v->height = s.height;
        v->time_base = AVRational{static_cast<int>(s.frameRate.den), static_cast<int>(s.frameRate.num)};
        v->framerate = AVRational{static_cast<int>(s.frameRate.num), static_cast<int>(s.frameRate.den)};
        v->pix_fmt = AV_PIX_FMT_YUV420P;
        v->color_range = AVCOL_RANGE_MPEG;
        if (!s.colorPrimaries.empty()) {
            const int value = av_color_primaries_from_name(s.colorPrimaries.c_str());
            if (value < 0) return makeError(ErrorCode::InvalidArgument, "codec", "Unknown colour primaries '" + s.colorPrimaries + "'.");
            v->color_primaries = static_cast<AVColorPrimaries>(value);
        }
        if (!s.colorTransfer.empty()) {
            const int value = av_color_transfer_from_name(s.colorTransfer.c_str());
            if (value < 0) return makeError(ErrorCode::InvalidArgument, "codec", "Unknown transfer function '" + s.colorTransfer + "'.");
            v->color_trc = static_cast<AVColorTransferCharacteristic>(value);
        }
        if (!s.colorMatrix.empty()) {
            const int value = av_color_space_from_name(s.colorMatrix.c_str());
            if (value < 0) return makeError(ErrorCode::InvalidArgument, "codec", "Unknown colour matrix '" + s.colorMatrix + "'.");
            v->colorspace = static_cast<AVColorSpace>(value);
        }
        v->gop_size = std::max(1, static_cast<int>(s.frameRate.toDouble() * 2));
        if (s.videoBitrate > 0) v->bit_rate = s.videoBitrate;
        else if (s.videoCodec == "mpeg4") v->bit_rate = static_cast<int64_t>(s.width) * s.height * 4;
        if (impl->format->oformat->flags & AVFMT_GLOBALHEADER) v->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* vopts = nullptr;
        if (s.videoCodec == "libx264" || s.videoCodec == "libx265") {
            if (s.videoBitrate <= 0) av_dict_set_int(&vopts, "crf", s.crf, 0);
            av_dict_set(&vopts, "preset", "medium", 0);
        }
        err = avcodec_open2(v, vcodec, &vopts);
        av_dict_free(&vopts);
        if (err < 0) return impl->encodeError("the " + s.videoCodec + " encoder could not be opened", err);
        avcodec_parameters_from_context(impl->videoStream->codecpar, v);
        impl->videoStream->time_base = v->time_base;
        impl->videoStream->avg_frame_rate = v->framerate;
    }

    if (s.audio) {
        const AVCodec* acodec = avcodec_find_encoder_by_name(s.audioCodec.c_str());
        if (!acodec) {
            return makeError(ErrorCode::NotFound, "codec", "The audio encoder '" + s.audioCodec + "' is not available.",
                             "Choose a different audio codec, e.g. 'aac'.");
        }
        impl->audioStream = avformat_new_stream(impl->format, nullptr);
        impl->audio.reset(avcodec_alloc_context3(acodec));
        AVCodecContext* a = impl->audio.get();
        a->sample_rate = s.sampleRate;
        av_channel_layout_default(&a->ch_layout, s.channels);
        a->sample_fmt = AV_SAMPLE_FMT_FLTP;
        a->bit_rate = s.audioBitrate;
        a->time_base = AVRational{1, s.sampleRate};
        if (impl->format->oformat->flags & AVFMT_GLOBALHEADER) a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if ((err = avcodec_open2(a, acodec, nullptr)) < 0)
            return impl->encodeError("the " + s.audioCodec + " encoder could not be opened", err);
        avcodec_parameters_from_context(impl->audioStream->codecpar, a);
        impl->audioStream->time_base = a->time_base;
    }

    if (!(impl->format->oformat->flags & AVFMT_NOFILE)) {
        err = avio_open(&impl->format->pb, path.string().c_str(), AVIO_FLAG_WRITE);
        if (err < 0) return impl->encodeError("the file could not be created", err);
    }
    if ((err = avformat_write_header(impl->format, nullptr)) < 0) return impl->encodeError("the header could not be written", err);
    impl->headerWritten = true;

    if (s.video) {
        AVCodecContext* v = impl->video.get();
        AVFrame* f = impl->videoFrame.get();
        f->format = v->pix_fmt;
        f->width = v->width;
        f->height = v->height;
        if ((err = av_frame_get_buffer(f, 0)) < 0) return impl->encodeError("video buffers could not be allocated", err);
    }
    UP_LOG_INFO(log::sub::Codec, "Writing " << path.string() << (s.video ? " with " + s.videoCodec : std::string(" (audio only)"))
                                            << " " << s.width << "x" << s.height << " @ " << s.frameRate.toString());
    return std::unique_ptr<MediaWriter>(new MediaWriter(std::move(impl)));
}

Status MediaWriter::writeVideo(const VideoFrame& frame) {
    Impl& m = *impl_;
    if (m.finished) return makeError(ErrorCode::InvalidArgument, "codec", "Cannot write after finish().");
    if (!m.video) return makeError(ErrorCode::InvalidArgument, "codec", "This file was opened without a video stream.");
    if (frame.width != m.settings.width || frame.height != m.settings.height) {
        return makeError(ErrorCode::InvalidArgument, "codec", "A rendered frame has the wrong size for this export.",
                         "Report this problem; the renderer and encoder disagree on the output size.",
                         std::to_string(frame.width) + "x" + std::to_string(frame.height));
    }
    int err = av_frame_make_writable(m.videoFrame.get());
    if (err < 0) return m.encodeError("the frame buffer is busy", err);
    m.sws = sws_getCachedContext(m.sws, frame.width, frame.height, AV_PIX_FMT_RGBA, frame.width, frame.height,
                                 AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!m.sws) return makeError(ErrorCode::EncodeError, "codec", "Unable to convert the frame to YUV for encoding.");
    if (m.sws != m.configuredSws) {
        // Full-range RGB in, limited-range YUV out with the tagged matrix (swscale's default is BT.601).
        const int* coefficients = sws_getCoefficients(ffmpeg::swsMatrixForName(m.settings.colorMatrix));
        sws_setColorspaceDetails(m.sws, coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);
        m.configuredSws = m.sws;
    }
    // Read from padded scratch memory: swscale may read past the end of a tightly packed row.
    const int stride = ffmpeg::alignedStride(frame.width * 4);
    m.scratch.resize(static_cast<std::size_t>(stride) * frame.height + ffmpeg::kSwsAlign);
    for (int y = 0; y < frame.height; ++y)
        std::memcpy(m.scratch.data() + static_cast<std::size_t>(y) * stride, frame.row(y), static_cast<std::size_t>(frame.width) * 4);
    const uint8_t* src[4] = {m.scratch.data(), nullptr, nullptr, nullptr};
    const int srcStride[4] = {stride, 0, 0, 0};
    sws_scale(m.sws, src, srcStride, 0, frame.height, m.videoFrame->data, m.videoFrame->linesize);
    m.videoFrame->pts = m.videoPts++;
    return m.send(m.video.get(), m.videoStream, m.videoFrame.get());
}

Status MediaWriter::writeAudio(const float* samples, int64_t frameCount) {
    Impl& m = *impl_;
    if (!m.audio) return Status::success();
    m.audioFifo.insert(m.audioFifo.end(), samples, samples + frameCount * m.settings.channels);
    const int chunk = m.audio->frame_size > 0 ? m.audio->frame_size : 1024;
    while (static_cast<int64_t>(m.audioFifo.size()) >= static_cast<int64_t>(chunk) * m.settings.channels)
        UP_TRY(m.encodeAudioChunk(chunk));
    return Status::success();
}

Status MediaWriter::finish() {
    Impl& m = *impl_;
    if (m.finished) return Status::success();
    if (m.audio) {
        const int remaining = static_cast<int>(m.audioFifo.size() / static_cast<std::size_t>(m.settings.channels));
        if (remaining > 0) {
            const int chunk = m.audio->frame_size > 0 ? m.audio->frame_size : remaining;
            // Pad the final partial frame with silence (encoders require fixed-size frames).
            m.audioFifo.resize(static_cast<std::size_t>(chunk) * m.settings.channels, 0.0f);
            UP_TRY(m.encodeAudioChunk(chunk));
        }
        UP_TRY(m.send(m.audio.get(), m.audioStream, nullptr));
    }
    if (m.video) UP_TRY(m.send(m.video.get(), m.videoStream, nullptr));
    const int err = av_write_trailer(m.format);
    if (err < 0) return m.encodeError("the file could not be finalised", err);
    m.finished = true;
    UP_LOG_INFO(log::sub::Codec, "Finished " << m.path.string() << " (" << m.videoPts << " frames)");
    return Status::success();
}

}  // namespace up
