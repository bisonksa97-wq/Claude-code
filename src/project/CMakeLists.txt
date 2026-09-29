up_add_module(up_project
    SOURCES
        Project.cpp
        ProjectMigrator.cpp
        ProjectSerializer.cpp
    DEPS up_core up_timeline nlohmann_json::nlohmann_json)
