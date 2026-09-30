up_add_module(up_render
    SOURCES
        AudioMixer.cpp
        ColorGrading.cpp
        Compositing.cpp
        ExportJob.cpp
        FrameCompositor.cpp
        MediaSource.cpp
        Scopes.cpp
    DEPS up_core up_audio up_timeline up_project up_codec)
