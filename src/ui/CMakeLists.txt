set(CMAKE_AUTOMOC ON)

add_library(up_ui STATIC
    MainWindow.cpp
    MediaPoolPanel.cpp
    Theme.cpp
    TimelineView.cpp
    ViewerPanel.cpp)
target_include_directories(up_ui PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(up_ui PUBLIC up_app Qt6::Widgets)
up_set_warnings(up_ui)

add_executable(ultimatepost-studio main.cpp)
target_link_libraries(ultimatepost-studio PRIVATE up_ui)
up_set_warnings(ultimatepost-studio)
