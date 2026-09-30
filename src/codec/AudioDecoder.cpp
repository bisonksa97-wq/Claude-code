#include "codec/AudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "codec/FFmpegCommon.h"
#include "core/Log.h"

namespace up {

struct AudioDecoder::Impl {
    std::filesystem::path path;
    ffmpeg::FormatInputPtr format;
    ffmpeg::CodecContextPtr codec;
    ffmpeg::PacketPtr packet{av_packet_alloc()};
    ffmpeg::FramePtr frame{av_frame_alloc()};
    SwrContext* swr = nullptr;
    int streamIndex = -1;
    AVRational timeBase{1, 1};
    int64_t startPts = 0;
    int outRate = 48000;
    int outChannels = 2;

    // Decoded, converted samples covering [bufferStart, bufferStart + buffer.size()/channels).
    std::vector<float> buffer;
    int64_t bufferStart = 0;
    int64_t nextSample = -1;  // output sample position of the next converted sample; -1 = unknown
    bool eof = false;
    bool draining = false;

    // Seeking is preferred over decoding forward beyond this many seconds.
    static constexpr double kForwardDecodeLimit = 2.0;

    ~Impl() { swr_free(&swr); }

    Error decodeError(const std::string& what, int err) const {
        return makeError(ErrorCode::DecodeError, "audio",
                         "Unable to decode audio from '" + path.filename().string() + "' because " + what + ".",
                         "The file may be damaged. Try re-importing it or converting it to WAV.",
                         ffmpeg::errorString(err));
    }

    int64_t bufferEnd() const { return bufferStart + static_cast<int64_t>(buffer.size()) / outChannels; }

    Status seek(int64_t sample) {
        // Seek a little early: transform codecs such as AAC need the preceding frame
        // (overlap-add pre-roll) before their output is correct.
        const int64_t preroll = outRate / 10;
        const double seconds = static_cast<double>(std::max<int64_t>(0, sample - preroll)) / outRate;
        const int64_t pts = startPts + static_cast<int64_t>(seconds / av_q2d(timeBase));
        int err = av_seek_frame(format.get(), streamIndex, pts, AVSEEK_FLAG_BACKWARD);
        if (err < 0) err = avformat_seek_file(format.get(), streamIndex, INT64_MIN, 0, 0, 0);
        if (err < 0) return decodeError("seeking failed", err);
        avcodec_flush_buffers(codec.get());
        swr_init(swr);  // drop resampler history
        buffer.clear();
        nextSample = -1;
        eof = draining = false;
        return Status::success();
    }

    // Decodes one frame and appends converted samples to the buffer. Returns false at EOF.
    Result<bool> decodeMore() {
        while (true) {
            int err = avcodec_receive_frame(codec.get(), frame.get());
            if (err == 0) break;
            if (err == AVERROR_EOF || (err == AVERROR(EAGAIN) && draining)) {
                eof = true;
                return false;
            }
            if (err != AVERROR(EAGAIN)) return decodeError("the decoder returned invalid data", err);
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
                if (err < 0 && err != AVERROR(EAGAIN) && err != AVERROR_INVALIDDATA)
                    return decodeError("a compressed packet was rejected", err);
                break;
            }
        }

        if (nextSample < 0) {
            const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : startPts;
            nextSample = static_cast<int64_t>(std::llround(static_cast<double>(pts - startPts) * av_q2d(timeBase) * outRate));
            bufferStart = nextSample;
            buffer.clear();
        }
        const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
        std::vector<float> converted(static_cast<std::size_t>(std::max(maxOut, 0)) * outChannels);
        uint8_t* outPlanes[1] = {reinterpret_cast<uint8_t*>(converted.data())};
        const int got = swr_convert(swr, outPlanes, maxOut, const_cast<const uint8_t**>(frame->extended_data),
                                    frame->nb_samples);
        if (got < 0) return decodeError("sample conversion failed", got);
        buffer.insert(buffer.end(), converted.begin(), converted.begin() + static_cast<std::ptrdiff_t>(got) * outChannels);
        nextSample += got;
        return true;
    }

    Status read(int64_t start, int64_t count, float* out) {
        std::fill(out, out + count * outChannels, 0.0f);
        if (count <= 0) return Status::success();
        if (start < 0) {
            const int64_t skip = std::min(count, -start);
            out += skip * outChannels;
            count -= skip;
            start = 0;
            if (count == 0) return Status::success();
        }
        const int64_t forwardLimit = static_cast<int64_t>(kForwardDecodeLimit * outRate);
        const bool haveData = nextSample >= 0;
        if (!haveData || start < bufferStart || start > bufferEnd() + forwardLimit) UP_TRY(seek(start));

        const int64_t end = start + count;
        while (!eof && (nextSample < 0 || bufferEnd() < end)) {
            auto more = decodeMore();
            if (!more.ok()) return more.error();
            if (!more.value()) break;
            // Drop samples we no longer need to bound memory during long sequential reads.
            if (bufferStart < start) {
                const int64_t drop = std::min<int64_t>(start - bufferStart, bufferEnd() - bufferStart);
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(drop) * outChannels);
                bufferStart += drop;
            }
        }
        const int64_t from = std::max(start, bufferStart);
        const int64_t to = std::min(end, bufferEnd());
        if (to > from) {
            std::memcpy(out + (from - start) * outChannels,
                        buffer.data() + (from - bufferStart) * outChannels,
                        static_cast<std::size_t>(to - from) * outChannels * sizeof(float));
        }
        return Status::success();
    }
};

AudioDecoder::AudioDecoder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AudioDecoder::~AudioDecoder() = default;

Result<std::unique_ptr<AudioDecoder>> AudioDecoder::open(const std::filesystem::path& path, int outSampleRate,
                                                         int outChannels) {
    ffmpeg::initialize();
    if (outSampleRate <= 0 || outChannels <= 0 || outChannels > 8) {
        return makeError(ErrorCode::InvalidArgument, "audio", "Invalid audio output configuration.");
    }
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->outRate = outSampleRate;
    impl->outChannels = outChannels;
    AVFormatContext* raw = nullptr;
    int err = avformat_open_input(&raw, path.string().c_str(), nullptr, nullptr);
    if (err < 0) {
        return makeError(ErrorCode::MediaOffline, "audio", "Unable to open '" + path.string() + "'.",
                         "Check that the file exists or relink the media.", ffmpeg::errorString(err));
    }
    impl->format.reset(raw);
    if ((err = avformat_find_stream_info(raw, nullptr)) < 0) return impl->decodeError("stream info is unreadable", err);
    const AVCodec* decoder = nullptr;
    impl->streamIndex = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (impl->streamIndex < 0 || !decoder) {
        return makeError(ErrorCode::DecodeError, "audio", "'" + path.filename().string() + "' has no audio stream.",
                         "Use this file on a video track instead.");
    }
    AVStream* stream = raw->streams[impl->streamIndex];
    impl->timeBase = stream->time_base;
    impl->startPts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    impl->codec.reset(avcodec_alloc_context3(decoder));
    avcodec_parameters_to_context(impl->codec.get(), stream->codecpar);
    if ((err = avcodec_open2(impl->codec.get(), decoder, nullptr)) < 0)
        return impl->decodeError("the audio decoder could not be opened", err);

    // Zero-initialised: av_channel_layout_copy uninitialises its destination first, so
    // stack garbage there would be freed (an intermittent crash seen in Release builds).
    AVChannelLayout outLayout{};
    av_channel_layout_default(&outLayout, outChannels);
    AVChannelLayout inLayout{};
    if (impl->codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || impl->codec->ch_layout.nb_channels == 0) {
        av_channel_layout_default(&inLayout, std::max(1, impl->codec->ch_layout.nb_channels));
    } else {
        av_channel_layout_copy(&inLayout, &impl->codec->ch_layout);
    }
    err = swr_alloc_set_opts2(&impl->swr, &outLayout, AV_SAMPLE_FMT_FLT, outSampleRate, &inLayout,
                              impl->codec->sample_fmt, impl->codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (err < 0 || (err = swr_init(impl->swr)) < 0) return impl->decodeError("the resampler could not be set up", err);
    UP_LOG_DEBUG(log::sub::Audio, "Opened audio decoder " << decoder->name << " for " << path.string());
    return std::unique_ptr<AudioDecoder>(new AudioDecoder(std::move(impl)));
}

int AudioDecoder::sampleRate() const { return impl_->outRate; }
int AudioDecoder::channels() const { return impl_->outChannels; }

Status AudioDecoder::read(int64_t startSample, int64_t frameCount, float* out) {
    return impl_->read(startSample, frameCount, out);
}

}  // namespace up
