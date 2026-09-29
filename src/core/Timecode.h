#pragma once

#include <optional>
#include <string>

#include "core/Rational.h"

namespace up {

// SMPTE timecode helpers. Drop-frame counting is used automatically for
// 30000/1001 and 60000/1001 when `dropFrame` is requested.
bool supportsDropFrame(FrameRate rate);
int nominalFps(FrameRate rate);

std::string formatTimecode(FrameIndex frame, FrameRate rate, bool dropFrame = false);
// Accepts "HH:MM:SS:FF" (or ';' before FF for drop-frame) and plain frame numbers.
std::optional<FrameIndex> parseTimecode(const std::string& text, FrameRate rate);

}  // namespace up
