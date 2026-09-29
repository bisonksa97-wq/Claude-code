include(GoogleTest)

add_library(up_test_support STATIC support/TestSupport.cpp)
target_include_directories(up_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(up_test_support PUBLIC up_app up_playback GTest::gtest)

function(up_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE up_test_support GTest::gtest_main)
    up_set_warnings(${name})
    gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60)
endfunction()

up_add_test(unit_tests
    unit/CacheJobsTest.cpp
    unit/CoreTest.cpp
    unit/CommandStackTest.cpp
    unit/TimelineOpsTest.cpp
    unit/ProjectFormatTest.cpp)

up_add_test(integration_tests
    integration/CodecTest.cpp
    integration/MediaAssetsTest.cpp
    integration/PlaybackTest.cpp
    integration/SessionTest.cpp
    integration/VerticalSliceTest.cpp)

if(UP_BUILD_UI)
    add_subdirectory(ui)
endif()
