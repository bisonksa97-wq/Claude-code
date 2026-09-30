set(CMAKE_AUTOMOC ON)

add_library(up_ui STATIC
    MainWindow.cpp
    MarkerDialog.cpp
    MediaPoolPanel.cpp
    Theme.cpp
    TimelineView.cpp
    ViewerPanel.cpp)
target_include_directories(up_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(up_ui PUBLIC up_app up_playback Qt6::Widgets)
if(TARGET Qt6::Multimedia)
    target_sources(up_ui PRIVATE QtAudioOutput.cpp)
    target_link_libraries(up_ui PUBLIC Qt6::Multimedia)
    target_compile_definitions(up_ui PUBLIC UP_HAVE_QT_MULTIMEDIA=1)
else()
    message(WARNING "Qt6 Multimedia not found: playback will be silent")
endif()
up_set_warnings(up_ui)

add_executable(ultimatepost-studio main.cpp)
target_link_libraries(ultimatepost-studio PRIVATE up_ui)
up_set_warnings(ultimatepost-studio)
