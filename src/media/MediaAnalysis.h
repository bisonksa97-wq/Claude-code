#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "codec/VideoFrame.h"
#include "core/CancelToken.h"
#include "core/MediaInfo.h"
#include "core/Result.h"

// Derived-media generators (thumbnails, waveform peaks) and their on-disk formats.
// Generators are pure functions of the file; caching and scheduling live in app/MediaAssets.
namespace up::media {

// Identifies the exact file contents a derived asset was generated from:
// absolute path, size and modification time. Changes whenever the file does.
Result<std::string> fileFingerprint(const std::filesystem::path& path);

// --- Thumbnails --------------------------------------------------------------

// Poster frame at 10% of the duration (capped at 5 s; frame 0 for stills), scaled to
// fit within maxWidth x maxHeight preserving aspect ratio.
Result<VideoFrame> generateThumbnail(const std::filesystem::path& path, const MediaInfo& info, int maxWidth = 192,
                                     int maxHeight = 108, const CancelToken* cancel = nullptr);

// Binary PPM (P6, RGB). Alpha is dropped on encode and set opaque on decode.
std::string encodePpm(const VideoFrame& frame);
Result<VideoFrame> decodePpm(const std::string& bytes);

// --- Waveforms ---------------------------------------------------------------

// Min/max envelope of a file's audio (all channels folded together), one peak per
// `samplesPerPeak` samples at `sampleRate`.
struct WaveformPeaks {
    int sampleRate = 48000;
    int samplesPerPeak = 480;  // 100 peaks per second at 48 kHz
    std::vector<int16_t> minMax;  // interleaved min,max per peak, scaled to [-32767, 32767]

    std::size_t peakCount() const { return minMax.size() / 2; }
    double secondsPerPeak() const { return static_cast<double>(samplesPerPeak) / sampleRate; }
    double durationSeconds() const { return static_cast<double>(peakCount()) * secondsPerPeak(); }
    // Envelope over [fromSeconds, toSeconds) as floats in [-1, 1]; {0,0} outside the media.
    std::pair<float, float> range(double fromSeconds, double toSeconds) const;
};

// Covers the probed duration in `info` (so silent passages are kept as silence).
Result<WaveformPeaks> generateWaveform(const std::filesystem::path& path, const MediaInfo& info,
                                       int sampleRate = 48000, int samplesPerPeak = 480,
                                       const CancelToken* cancel = nullptr);

std::string encodeWaveform(const WaveformPeaks& peaks);
Result<WaveformPeaks> decodeWaveform(const std::string& bytes);

}  // namespace up::media
