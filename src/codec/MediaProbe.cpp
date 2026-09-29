#include "codec/MediaProbe.h"

#include <mutex>

extern "C" {
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
}

#include "codec/FFmpegCommon.h"
#include "core/Log.h"

namespace up {

namespace ffmpeg {
void initialize() {
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_ERROR); });
}
}  // namespace ffmpeg

Result<MediaInfo> probeMedia(const std::filesystem::path& path) {
    ffmpeg::initialize();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return makeError(ErrorCode::NotFound, "media", "The file '" + path.string() + "' does not exist.",
                         "Check the path, or relink the media if it was moved.");
    }
    AVFormatContext* raw = nullptr;
    int err = avformat_open_input(&raw, path.string().c_str(), nullptr, nullptr);
    if (err < 0) {
        return makeError(ErrorCode::DecodeError, "codec",
                         "Unable to open '" + path.filename().string() + "' because its format is not recognised.",
                         "Convert the file to a common format (e.g. MP4/H.264, MOV/ProRes, WAV) and import it again.",
                         ffmpeg::errorString(err));
    }
    ffmpeg::FormatInputPtr fmt(raw);
    err = avformat_find_stream_info(fmt.get(), nullptr);
    if (err < 0) {
        return makeError(ErrorCode::DecodeError, "codec",
                         "Unable to read stream information from '" + path.filename().string() + "'.",
                         "The file may be damaged or incomplete.", ffmpeg::errorString(err));
    }

    MediaInfo info;
    info.container = fmt->iformat ? fmt->iformat->name : "";
    info.fileSize = static_cast<int64_t>(std::filesystem::file_size(path, ec));
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        info.durationSeconds = static_cast<double>(fmt->duration) / AV_TIME_BASE;

    const int vIndex = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIndex >= 0) {
        const AVStream* s = fmt->streams[vIndex];
        const AVCodecParameters* par = s->codecpar;
        const bool attachedPicture = (s->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        if (!attachedPicture) {
            info.hasVideo = true;
            info.videoCodec = avcodec_get_name(par->codec_id);
            info.width = par->width;
            info.height = par->height;
            const AVRational fr = s->avg_frame_rate.num > 0 ? s->avg_frame_rate : s->r_frame_rate;
            info.frameRate = Rational(fr.num, fr.den > 0 ? fr.den : 1);
            if (const char* pf = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) info.pixelFormat = pf;
            // Image formats (png, jpeg...) demux as a single frame: treat them as stills.
            const std::string demuxer = info.container;
            info.isStill = demuxer.find("_pipe") != std::string::npos || demuxer == "image2" ||
                           (s->nb_frames == 1);
            if (s->duration != AV_NOPTS_VALUE && s->duration > 0 && !info.isStill)
                info.durationSeconds = std::max(info.durationSeconds, static_cast<double>(s->duration) * av_q2d(s->time_base));
        }
        if (const AVDictionaryEntry* tc = av_dict_get(s->metadata, "timecode", nullptr, 0)) info.timecode = tc->value;
    }
    if (info.timecode.empty()) {
        if (const AVDictionaryEntry* tc = av_dict_get(fmt->metadata, "timecode", nullptr, 0)) info.timecode = tc->value;
    }

    const int aIndex = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (aIndex >= 0) {
        const AVCodecParameters* par = fmt->streams[aIndex]->codecpar;
        info.hasAudio = true;
        info.audioCodec = avcodec_get_name(par->codec_id);
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;
    }

    if (!info.hasVideo && !info.hasAudio) {
        return makeError(ErrorCode::DecodeError, "media",
                         "'" + path.filename().string() + "' contains no video or audio streams.",
                         "Import a video, audio or image file.");
    }
    UP_LOG_DEBUG(log::sub::Codec, "Probed " << path.string() << ": " << info.container << " " << info.width << "x"
                                            << info.height << " " << info.durationSeconds << "s");
    return info;
}

}  // namespace up
