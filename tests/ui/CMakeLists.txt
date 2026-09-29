add_executable(ui_tests UiSmokeTest.cpp)
target_link_libraries(ui_tests PRIVATE up_ui up_test_support Qt6::Test GTest::gtest)
up_set_warnings(ui_tests)
gtest_discover_tests(ui_tests DISCOVERY_TIMEOUT 60)
