up_add_module(up_playback
    SOURCES
        PlaybackEngine.cpp
        SampleFifo.cpp
    DEPS up_core up_project up_render Threads::Threads)
