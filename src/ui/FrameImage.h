#pragma once

#include <QImage>

#include "codec/VideoFrame.h"

namespace up::ui {

// Deep-copies an engine frame into a QImage (the UI never keeps pointers into engine buffers).
inline QImage toQImage(const VideoFrame& frame) {
    if (frame.empty()) return {};
    return QImage(frame.pixels.data(), frame.width, frame.height, frame.width * 4, QImage::Format_RGBA8888).copy();
}

}  // namespace up::ui
