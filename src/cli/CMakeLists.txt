add_executable(ultimatepost main.cpp Args.cpp)
target_link_libraries(ultimatepost PRIVATE up_app)
up_set_warnings(ultimatepost)
