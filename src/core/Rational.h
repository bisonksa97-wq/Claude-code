#pragma once

#include <cstdint>
#include <numeric>
#include <string>

namespace up {

// Exact rational number used for frame rates and time bases (e.g. 30000/1001).
struct Rational {
    int64_t num = 0;
    int64_t den = 1;

    constexpr Rational() = default;
    constexpr Rational(int64_t n, int64_t d = 1) : num(n), den(d) { normalize(); }

    constexpr void normalize() {
        if (den < 0) {
            num = -num;
            den = -den;
        }
        if (den == 0) return;
        const int64_t g = std::gcd(num < 0 ? -num : num, den);
        if (g > 1) {
            num /= g;
            den /= g;
        }
    }

    constexpr bool valid() const { return den > 0 && num > 0; }
    constexpr double toDouble() const { return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den); }

    friend constexpr bool operator==(const Rational& a, const Rational& b) { return a.num == b.num && a.den == b.den; }

    std::string toString() const { return std::to_string(num) + "/" + std::to_string(den); }
    // Parses "25", "25/1" or "30000/1001". Returns an invalid Rational on failure.
    static Rational parse(const std::string& text);
};

using FrameRate = Rational;
// Position or length on a timeline, in frames of that timeline's frame rate.
using FrameIndex = int64_t;

// Converts a frame count at `rate` to seconds.
inline double framesToSeconds(FrameIndex frames, FrameRate rate) {
    return static_cast<double>(frames) * static_cast<double>(rate.den) / static_cast<double>(rate.num);
}

// Converts seconds to the index of the frame that contains that instant.
FrameIndex secondsToFrames(double seconds, FrameRate rate);

// Index of the first audio sample belonging to timeline frame `frame`.
// Computed exactly so that per-frame sample counts never drift.
int64_t frameToSample(FrameIndex frame, FrameRate rate, int sampleRate);

}  // namespace up
