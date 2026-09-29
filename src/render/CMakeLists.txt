up_add_module(up_render
    SOURCES
        AudioMixer.cpp
        ExportJob.cpp
        FrameCompositor.cpp
        MediaSource.cpp
    DEPS up_core up_timeline up_project up_codec)
