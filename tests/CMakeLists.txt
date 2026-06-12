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
# 测试9:rotating_file 性能测试
add_executable(test_rotating_file test_rotating_file.cpp)
target_link_libraries(test_rotating_file PRIVATE minispdlog Threads::Threads)
# 测试10:性能测试
add_executable(test_performance  test_performance.cpp)
target_link_libraries(test_performance PRIVATE minispdlog Threads::Threads)
# 测试11:编译期级别控制测试
add_executable(test_compile_level test_compile_level.cpp)
target_link_libraries(test_compile_level PRIVATE minispdlog Threads::Threads)

# 测试13:SPSC 无锁队列测试
add_executable(test_spsc_queue test_spsc_queue.cpp)
target_link_libraries(test_spsc_queue PRIVATE minispdlog Threads::Threads)

# 队列对比 benchmark
add_executable(benchmark_queue benchmark_queue.cpp)
target_link_libraries(benchmark_queue PRIVATE minispdlog Threads::Threads benchmark)

add_executable(benchmark_spdlog  benchmark_spdlog.cpp)
target_include_directories(benchmark_spdlog PRIVATE ~/spdlog/include)
target_link_libraries(benchmark_spdlog PRIVATE minispdlog Threads::Threads)