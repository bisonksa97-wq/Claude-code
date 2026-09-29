up_add_module(up_codec
    SOURCES
        AudioDecoder.cpp
        MediaProbe.cpp
        MediaWriter.cpp
        VideoDecoder.cpp
        VideoFrame.cpp
    DEPS up_core PkgConfig::FFMPEG)
