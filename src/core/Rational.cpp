#include "core/Rational.h"

#include <cmath>
#include <stdexcept>

namespace up {

Rational Rational::parse(const std::string& text) {
    try {
        const auto slash = text.find('/');
        if (slash == std::string::npos) {
            std::size_t used = 0;
            const long long n = std::stoll(text, &used);
            if (used != text.size()) return {0, 0};
            return {n, 1};
        }
        std::size_t usedN = 0;
        std::size_t usedD = 0;
        const std::string numText = text.substr(0, slash);
        const std::string denText = text.substr(slash + 1);
        const long long n = std::stoll(numText, &usedN);
        const long long d = std::stoll(denText, &usedD);
        if (usedN != numText.size() || usedD != denText.size() || d == 0) return {0, 0};
        return {n, d};
    } catch (const std::exception&) {
        return {0, 0};
    }
}

FrameIndex secondsToFrames(double seconds, FrameRate rate) {
    // The small epsilon absorbs floating point error for instants that fall exactly on a frame boundary.
    const double frames = seconds * static_cast<double>(rate.num) / static_cast<double>(rate.den);
    return static_cast<FrameIndex>(std::floor(frames + 1e-6));
}

int64_t frameToSample(FrameIndex frame, FrameRate rate, int sampleRate) {
    // frame * den * sampleRate fits in 64 bits for any realistic timeline
    // (e.g. 10^8 frames * 1001 * 192000 < 2^63).
    return frame * rate.den * sampleRate / rate.num;
}

}  // namespace up
