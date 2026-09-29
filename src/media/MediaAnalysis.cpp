#include "media/MediaAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "codec/AudioDecoder.h"
#include "codec/VideoDecoder.h"

namespace up::media {
namespace fs = std::filesystem;

namespace {

constexpr char kWaveformMagic[4] = {'U', 'P', 'W', 'F'};
constexpr uint32_t kWaveformVersion = 1;

Error cancelledError() {
    return makeError(ErrorCode::Cancelled, "media", "Media analysis was cancelled.");
}

Error corrupt(const std::string& what) {
    return makeError(ErrorCode::ParseError, "cache", "A cached " + what + " is damaged and will be regenerated.");
}

template <typename T>
void put(std::string& out, T v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool get(const std::string& in, std::size_t& pos, T& v) {
    if (pos + sizeof(T) > in.size()) return false;
    std::memcpy(&v, in.data() + pos, sizeof(T));
    pos += sizeof(T);
    return true;
}

}  // namespace

Result<std::string> fileFingerprint(const fs::path& path) {
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec).lexically_normal();
    const auto size = fs::file_size(absolute, ec);
    if (ec) {
        return makeError(ErrorCode::MediaOffline, "media", "'" + path.filename().string() + "' is not available.",
                         "Relink the media.", ec.message());
    }
    const auto mtime = fs::last_write_time(absolute, ec).time_since_epoch().count();
    std::ostringstream os;
    os << absolute.generic_string() << '|' << size << '|' << mtime;
    return os.str();
}

Result<VideoFrame> generateThumbnail(const fs::path& path, const MediaInfo& info, int maxWidth, int maxHeight,
                                     const CancelToken* cancel) {
    if (!info.hasVideo || info.width <= 0 || info.height <= 0) {
        return makeError(ErrorCode::InvalidArgument, "media", "'" + path.filename().string() + "' has no picture to preview.");
    }
    if (cancel && cancel->cancelled()) return cancelledError();
    const double scale = std::min(static_cast<double>(maxWidth) / info.width, static_cast<double>(maxHeight) / info.height);
    const int w = std::max(2, static_cast<int>(std::lround(info.width * std::min(1.0, scale))) & ~1);
    const int h = std::max(2, static_cast<int>(std::lround(info.height * std::min(1.0, scale))) & ~1);
    auto decoder = VideoDecoder::open(path);
    if (!decoder.ok()) return decoder.error();
    const double at = info.isStill ? 0.0 : std::min(5.0, info.durationSeconds * 0.1);
    return decoder.value()->frameAt(at, w, h);
}

std::string encodePpm(const VideoFrame& frame) {
    std::string out = "P6\n" + std::to_string(frame.width) + " " + std::to_string(frame.height) + "\n255\n";
    out.reserve(out.size() + static_cast<std::size_t>(frame.width) * frame.height * 3);
    for (std::size_t i = 0; i + 3 < frame.pixels.size(); i += 4)
        out.append(reinterpret_cast<const char*>(&frame.pixels[i]), 3);
    return out;
}

Result<VideoFrame> decodePpm(const std::string& bytes) {
    std::istringstream in(bytes);
    std::string magic;
    int w = 0, h = 0, maxval = 0;
    in >> magic >> w >> h >> maxval;
    if (magic != "P6" || w <= 0 || h <= 0 || w > 16384 || h > 16384 || maxval != 255) return corrupt("thumbnail");
    in.get();  // single whitespace after the header
    const auto offset = static_cast<std::size_t>(in.tellg());
    if (bytes.size() < offset + static_cast<std::size_t>(w) * h * 3) return corrupt("thumbnail");
    VideoFrame frame(w, h);
    const auto* src = reinterpret_cast<const uint8_t*>(bytes.data() + offset);
    for (std::size_t p = 0; p < static_cast<std::size_t>(w) * h; ++p) {
        frame.pixels[p * 4] = src[p * 3];
        frame.pixels[p * 4 + 1] = src[p * 3 + 1];
        frame.pixels[p * 4 + 2] = src[p * 3 + 2];
        frame.pixels[p * 4 + 3] = 255;
    }
    return frame;
}

std::pair<float, float> WaveformPeaks::range(double fromSeconds, double toSeconds) const {
    const double spp = secondsPerPeak();
    if (spp <= 0 || peakCount() == 0) return {0.0f, 0.0f};
    auto first = static_cast<int64_t>(std::floor(fromSeconds / spp));
    auto last = static_cast<int64_t>(std::ceil(toSeconds / spp));  // exclusive
    first = std::max<int64_t>(first, 0);
    last = std::min<int64_t>(std::max(last, first + 1), static_cast<int64_t>(peakCount()));
    if (first >= last) return {0.0f, 0.0f};
    int lo = 32767, hi = -32767;
    for (int64_t i = first; i < last; ++i) {
        lo = std::min<int>(lo, minMax[static_cast<std::size_t>(i * 2)]);
        hi = std::max<int>(hi, minMax[static_cast<std::size_t>(i * 2 + 1)]);
    }
    return {static_cast<float>(lo) / 32767.0f, static_cast<float>(hi) / 32767.0f};
}

Result<WaveformPeaks> generateWaveform(const fs::path& path, const MediaInfo& info, int sampleRate,
                                       int samplesPerPeak, const CancelToken* cancel) {
    if (sampleRate <= 0 || samplesPerPeak <= 0) {
        return makeError(ErrorCode::InvalidArgument, "media", "Invalid waveform resolution.");
    }
    if (!info.hasAudio || info.durationSeconds <= 0) {
        return makeError(ErrorCode::InvalidArgument, "media", "'" + path.filename().string() + "' has no audio to draw.");
    }
    auto decoder = AudioDecoder::open(path, sampleRate, 2);
    if (!decoder.ok()) return decoder.error();
    WaveformPeaks peaks;
    peaks.sampleRate = sampleRate;
    peaks.samplesPerPeak = samplesPerPeak;

    const auto totalFrames = static_cast<int64_t>(std::ceil(info.durationSeconds * sampleRate));
    const int64_t peakTotal = (totalFrames + samplesPerPeak - 1) / samplesPerPeak;
    peaks.minMax.reserve(static_cast<std::size_t>(peakTotal) * 2);
    // Decode about one second at a time, in whole peaks, checking for cancellation between blocks.
    const int64_t peaksPerBlock = std::max<int64_t>(1, sampleRate / samplesPerPeak);
    std::vector<float> buffer(static_cast<std::size_t>(peaksPerBlock * samplesPerPeak) * 2);
    for (int64_t peak = 0; peak < peakTotal; peak += peaksPerBlock) {
        if (cancel && cancel->cancelled()) return cancelledError();
        const int64_t count = std::min(peaksPerBlock, peakTotal - peak);
        UP_TRY(decoder.value()->read(peak * samplesPerPeak, count * samplesPerPeak, buffer.data()));
        for (int64_t p = 0; p < count; ++p) {
            float lo = 0.0f, hi = 0.0f;
            const float* s = buffer.data() + p * samplesPerPeak * 2;
            for (int64_t i = 0; i < samplesPerPeak * 2; ++i) {
                lo = std::min(lo, s[i]);
                hi = std::max(hi, s[i]);
            }
            peaks.minMax.push_back(static_cast<int16_t>(std::lround(std::clamp(lo, -1.0f, 1.0f) * 32767.0f)));
            peaks.minMax.push_back(static_cast<int16_t>(std::lround(std::clamp(hi, -1.0f, 1.0f) * 32767.0f)));
        }
    }
    return peaks;
}

std::string encodeWaveform(const WaveformPeaks& peaks) {
    std::string out(kWaveformMagic, 4);
    put<uint32_t>(out, kWaveformVersion);
    put<uint32_t>(out, static_cast<uint32_t>(peaks.sampleRate));
    put<uint32_t>(out, static_cast<uint32_t>(peaks.samplesPerPeak));
    put<uint64_t>(out, peaks.peakCount());
    out.append(reinterpret_cast<const char*>(peaks.minMax.data()), peaks.minMax.size() * sizeof(int16_t));
    return out;
}

Result<WaveformPeaks> decodeWaveform(const std::string& bytes) {
    if (bytes.size() < 4 || std::memcmp(bytes.data(), kWaveformMagic, 4) != 0) return corrupt("waveform");
    std::size_t pos = 4;
    uint32_t version = 0, rate = 0, spp = 0;
    uint64_t count = 0;
    if (!get(bytes, pos, version) || version != kWaveformVersion || !get(bytes, pos, rate) || !get(bytes, pos, spp) ||
        !get(bytes, pos, count) || rate == 0 || spp == 0) {
        return corrupt("waveform");
    }
    if (bytes.size() - pos != count * 2 * sizeof(int16_t)) return corrupt("waveform");
    WaveformPeaks peaks;
    peaks.sampleRate = static_cast<int>(rate);
    peaks.samplesPerPeak = static_cast<int>(spp);
    peaks.minMax.resize(static_cast<std::size_t>(count) * 2);
    std::memcpy(peaks.minMax.data(), bytes.data() + pos, peaks.minMax.size() * sizeof(int16_t));
    return peaks;
}

}  // namespace up::media
