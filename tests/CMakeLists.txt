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
# 测试5:registry测试
add_executable(test_registry test_registry.cpp)
target_link_libraries(test_registry PRIVATE minispdlog Threads::Threads)
# 测试6:thread pool测试
add_executable(test_thread_pool test_thread_pool.cpp)
target_link_libraries(test_thread_pool PRIVATE minispdlog Threads::Threads)
# 测试7:性能对比测试
add_executable(test_benchmark test_benchmark.cpp)
target_link_libraries(test_benchmark PRIVATE minispdlog Threads::Threads)
# 测试8:Google Benchmark 性能测试
add_executable(benchmark_async benchmark_async.cpp)
target_link_libraries(benchmark_async PRIVATE minispdlog Threads::Threads benchmark)

