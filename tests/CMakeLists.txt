find_package(Threads REQUIRED)

# ============================================================
# minispdlog 工业化测试框架 CMake 配置
#
# 设计原则：
#   1. 单入口可执行文件（编译一次，运行所有测试）
#   2. doctest header-only 测试内核（零外部依赖）
#   3. CTest 自动发现测试用例（支持 ctest 命令）
#   4. 可选 Google Benchmark 性能基准（保留已有 benchmark）
#   5. 代码覆盖率支持（gcov + lcov + genhtml）
# ============================================================

set(TEST_FRAMEWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/framework)

# ---- 单元测试源文件（按组件组织） ----
set(UNIT_TEST_SOURCES
    unit/test_level.cpp
    unit/test_sink.cpp
    unit/test_logger.cpp
    unit/test_registry.cpp
    unit/test_pattern.cpp
    unit/test_queue.cpp
    unit/test_async.cpp
    unit/test_utils.cpp
    unit/test_callback_sink.cpp
    unit/test_daily_file.cpp
    unit/test_rotating_file.cpp
    unit/test_json.cpp
    unit/test_buffered_file.cpp
    unit/test_network.cpp
    unit/test_qt_sink.cpp
)

# ---- 统一测试入口可执行文件 ----
# 编译一次，包含所有测试；doctest 提供自动测试发现和命令行过滤
# 比当前项目"每个文件独立可执行文件"的方式编译速度快 5-10 倍
add_executable(minispdlog_tests
    test_main.cpp
    ${UNIT_TEST_SOURCES}
)

target_include_directories(minispdlog_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}       # 使得 #include "framework/doctest.h" 能找到 tests/framework/
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(minispdlog_tests PRIVATE
    minispdlog
    Threads::Threads
)

# Qt GUI Sink 测试（offscreen platform，无需显示器）
if(MINISPDLOG_WITH_QT AND MINISPDLOG_QT_FOUND)
    target_link_libraries(minispdlog_tests PRIVATE ${MINISPDLOG_QT_TARGET})
    target_compile_definitions(minispdlog_tests PRIVATE MINISPDLOG_WITH_QT=1)
    set_target_properties(minispdlog_tests PROPERTIES AUTOMOC ON)
    message(STATUS "minispdlog_tests: Qt sink tests enabled")
endif()

# ---- CTest 集成 ----
# doctest 虽然没有 CMake 模块，但我们可以通过命令行参数实现测试过滤：
#   ./minispdlog_tests "[sink]"      # 只跑 sink 标签的测试
#   ./minispdlog_tests -tc="logger"  # 只跑名字包含 logger 的测试
#   ctest -R minispdlog_tests        # 运行整个测试套件
enable_testing()
add_test(NAME minispdlog_tests COMMAND minispdlog_tests)

# 为了更精细的 CTest 集成（每个 TEST_CASE 一个 CTest 项），
# 可以后续引入 doctest 官方 CMake 模块（doctest.cmake + doctestAddTests.cmake）
# 下载地址：https://github.com/doctest/doctest/blob/master/scripts/cmake/doctest.cmake

# ============================================================
# 可选：Google Benchmark 性能测试（保留已有测试）
# ============================================================
find_package(benchmark QUIET)
if(benchmark_FOUND)
    add_executable(minispdlog_bench_async benchmark_async.cpp)
    target_link_libraries(minispdlog_bench_async PRIVATE
        minispdlog
        Threads::Threads
        benchmark::benchmark
    )

    add_executable(minispdlog_bench_queue benchmark_queue.cpp)
    target_link_libraries(minispdlog_bench_queue PRIVATE
        minispdlog
        Threads::Threads
        benchmark::benchmark
    )
endif()

# ============================================================
# 代码覆盖率（gcov + lcov + genhtml）
# 启用方式：cmake -DMINISPDLOG_ENABLE_COVERAGE=ON ..
# 生成报告：cmake --build build --target coverage
# ============================================================
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    option(MINISPDLOG_ENABLE_COVERAGE "Enable coverage reporting" OFF)
    if(MINISPDLOG_ENABLE_COVERAGE)
        message(STATUS "Coverage enabled for minispdlog_tests")

        target_compile_options(minispdlog PRIVATE --coverage -O0 -fprofile-update=atomic)
        target_link_options(minispdlog PUBLIC --coverage)
        target_compile_options(minispdlog_tests PRIVATE --coverage -O0 -fprofile-update=atomic)
        target_link_options(minispdlog_tests PRIVATE --coverage)

        find_program(LCOV_PATH lcov)
        find_program(GENHTML_PATH genhtml)

        if(LCOV_PATH AND GENHTML_PATH)
            add_custom_target(coverage
                COMMAND ${CMAKE_COMMAND} -E env bash
                    "${PROJECT_SOURCE_DIR}/scripts/coverage.sh"
                    "${CMAKE_BINARY_DIR}"
                    "${LCOV_PATH}"
                    "${GENHTML_PATH}"
                    "${CMAKE_CTEST_COMMAND}"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                DEPENDS minispdlog_tests
                COMMENT "Generating coverage report (this may take a while)..."
            )
        else()
            message(WARNING "lcov or genhtml not found. Coverage target will not be available.")
        endif()
    endif()
endif()

# ============================================================
# 旧测试文件清理清单（已废弃，保留参考）
# ============================================================
# 以下文件已被新框架的 unit/*.cpp 替代，可以安全删除：
#   test_level.cpp        -> unit/test_level.cpp
#   test_sink.cpp         -> unit/test_sink.cpp
#   test_formatter.cpp    -> 待合并到 unit/test_pattern.cpp
#   test_logger.cpp       -> unit/test_logger.cpp
#   test_registry.cpp     -> unit/test_registry.cpp
#   test_thread_pool.cpp  -> 待合并到 unit/test_async.cpp
#   test_rotating_file.cpp-> unit/test_rotating_file.cpp
#   test_compile_level.cpp-> 待合并到 unit/test_level.cpp [compile]
#   test_performance.cpp  -> 已废弃，benchmark 替代
#   test_benchmark.cpp    -> 已废弃，benchmark 替代
#   test_spsc_queue.cpp   -> 待合并到 unit/test_queue.cpp
#   benchmark_async.cpp   -> 保留，见上方 benchmark target
#   benchmark_queue.cpp   -> 保留，见上方 benchmark target
#   benchmark_spdlog.cpp-> 保留，见上方 benchmark target
