up_add_module(up_app
    SOURCES
        EditorSession.cpp
        MediaAssets.cpp
        SourceProject.cpp
        ProjectCommands.cpp
    DEPS up_core up_timeline up_project up_media up_render up_cache up_jobs)
