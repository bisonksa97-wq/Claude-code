#include "core/Timecode.h"

#include <cmath>
#include <cstdio>
#include <regex>

namespace up {

bool supportsDropFrame(FrameRate rate) {
    return (rate.num == 30000 || rate.num == 60000) && rate.den == 1001;
}

int nominalFps(FrameRate rate) {
    return static_cast<int>(std::lround(rate.toDouble()));
}

std::string formatTimecode(FrameIndex frame, FrameRate rate, bool dropFrame) {
    const bool negative = frame < 0;
    if (negative) frame = -frame;
    const int fps = std::max(1, nominalFps(rate));
    const bool df = dropFrame && supportsDropFrame(rate);
    if (df) {
        // Standard SMPTE drop-frame conversion: skip frame numbers 0 and 1 (or 0-3 at 60p)
        // at the start of every minute except every tenth minute.
        const int64_t dropPerMinute = fps == 60 ? 4 : 2;
        const int64_t framesPer10Min = static_cast<int64_t>(fps) * 600 - dropPerMinute * 9;
        const int64_t framesPerMin = static_cast<int64_t>(fps) * 60 - dropPerMinute;
        const int64_t d = frame / framesPer10Min;
        const int64_t m = frame % framesPer10Min;
        if (m > dropPerMinute) {
            frame += dropPerMinute * 9 * d + dropPerMinute * ((m - dropPerMinute) / framesPerMin);
        } else {
            frame += dropPerMinute * 9 * d;
        }
    }
    const int64_t ff = frame % fps;
    const int64_t totalSeconds = frame / fps;
    const int64_t ss = totalSeconds % 60;
    const int64_t mm = (totalSeconds / 60) % 60;
    const int64_t hh = totalSeconds / 3600;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%s%02lld:%02lld:%02lld%c%02lld", negative ? "-" : "",
                  static_cast<long long>(hh), static_cast<long long>(mm), static_cast<long long>(ss),
                  df ? ';' : ':', static_cast<long long>(ff));
    return buffer;
}

std::optional<FrameIndex> parseTimecode(const std::string& text, FrameRate rate) {
    static const std::regex frameOnly(R"(^\d+$)");
    static const std::regex tc(R"(^(\d{1,3}):(\d{2}):(\d{2})([:;])(\d{2,3})$)");
    if (!rate.valid()) return std::nullopt;
    if (std::regex_match(text, frameOnly)) return std::stoll(text);
    std::smatch m;
    if (!std::regex_match(text, m, tc)) return std::nullopt;
    const int64_t hh = std::stoll(m[1]);
    const int64_t mm = std::stoll(m[2]);
    const int64_t ss = std::stoll(m[3]);
    const bool df = m[4] == ";" && supportsDropFrame(rate);
    const int64_t ff = std::stoll(m[5]);
    const int fps = std::max(1, nominalFps(rate));
    if (mm >= 60 || ss >= 60 || ff >= fps) return std::nullopt;
    int64_t frames = ((hh * 60 + mm) * 60 + ss) * fps + ff;
    if (df) {
        const int64_t dropPerMinute = fps == 60 ? 4 : 2;
        const int64_t totalMinutes = hh * 60 + mm;
        frames -= dropPerMinute * (totalMinutes - totalMinutes / 10);
    }
    return frames;
}

}  // namespace up
