find_package(Threads REQUIRED)
# 测试1:日志级别测试
add_executable(test_level test_level.cpp)
target_link_libraries(test_level PRIVATE minispdlog)
# 测试2:日志格式测试
add_executable(test_sink test_sink.cpp)
target_link_libraries(test_sink PRIVATE minispdlog )
# 测试3:多线程测试
add_executable(test_formatter test_formatter.cpp)
target_link_libraries(test_formatter PRIVATE minispdlog Threads::Threads)
# 测试4:logger测试
add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger PRIVATE minispdlog Threads::Threads)
