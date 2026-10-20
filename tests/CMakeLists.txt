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
    unit/test_coro.cpp
)

# ---- 统一测试入口可执行文件 ----
# 编译一次，包含所有测试；doctest 提供自动测试发现和命令行过滤
# 相比“每个文件独立生成可执行文件”，可避免重复编译并缩短构建时间。
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

# Windows 沙箱的系统临时目录可能允许创建文件却拒绝原子重命名。测试目录放在
# 当前构建树中，既保留真实的轮转语义，也避免依赖宿主临时目录权限。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tmp")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/tmp" MINISPDLOG_TEST_TMP_DIR)
target_compile_definitions(minispdlog_tests PRIVATE
    MINISPDLOG_TEST_TMP_DIR="${MINISPDLOG_TEST_TMP_DIR}"
)

# Qt GUI Sink 测试（offscreen platform，无需显示器）
if(MINISPDLOG_WITH_QT AND MINISPDLOG_QT_FOUND)
    target_link_libraries(minispdlog_tests PRIVATE ${MINISPDLOG_QT_TARGET})
    target_compile_definitions(minispdlog_tests PRIVATE MINISPDLOG_WITH_QT=1)
    set_target_properties(minispdlog_tests PROPERTIES AUTOMOC ON)
    message(STATUS "minispdlog_tests: Qt sink tests enabled")
endif()

# ---- CTest 集成 ----
# 单个可执行文件只编译一次；CTest 按源文件切分组件条目。
# 默认 ctest 中每个 TEST_CASE 只执行一次，同时失败报告可直接定位组件。
enable_testing()

function(minispdlog_add_component_test component source_file labels)
    add_test(
        NAME minispdlog.${component}
        COMMAND $<TARGET_FILE:minispdlog_tests> "--source-file=*${source_file}"
    )
    set_tests_properties(minispdlog.${component} PROPERTIES
        LABELS "${labels}"
        TIMEOUT 60
    )
endfunction()

minispdlog_add_component_test(level         test_level.cpp         "unit;core")
minispdlog_add_component_test(sink          test_sink.cpp          "unit;sink")
minispdlog_add_component_test(logger        test_logger.cpp        "unit;core;concurrency")
minispdlog_add_component_test(registry      test_registry.cpp      "unit;core;concurrency")
minispdlog_add_component_test(pattern       test_pattern.cpp       "unit;formatter")
minispdlog_add_component_test(queue         test_queue.cpp         "unit;queue;concurrency")
minispdlog_add_component_test(async         test_async.cpp         "unit;async;concurrency")
minispdlog_add_component_test(utils         test_utils.cpp         "unit;core")
minispdlog_add_component_test(callback      test_callback_sink.cpp "unit;sink")
minispdlog_add_component_test(daily_file    test_daily_file.cpp    "unit;sink;filesystem")
minispdlog_add_component_test(rotating_file test_rotating_file.cpp "unit;sink;filesystem")
minispdlog_add_component_test(json          test_json.cpp          "unit;formatter;sink;filesystem")
minispdlog_add_component_test(buffered_file test_buffered_file.cpp "unit;sink;filesystem")
minispdlog_add_component_test(network       test_network.cpp       "unit;sink;network")
minispdlog_add_component_test(qt_sink       test_qt_sink.cpp       "unit;sink;optional")
minispdlog_add_component_test(coro          test_coro.cpp          "unit;async;coro;concurrency")

# 需要与本地 doctest 命令行完全一致时，可直接运行下列目标。
add_custom_target(minispdlog_check
    COMMAND $<TARGET_FILE:minispdlog_tests>
    DEPENDS minispdlog_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    USES_TERMINAL
)

# ---- 无外部依赖的性能烟雾测量 ----
# 只输出固定消息数下的耗时与吞吐量，不设置会受机器负载影响的硬阈值。
option(MINISPDLOG_BUILD_PERF_SMOKE "构建无外部依赖的性能烟雾测量" OFF)
if(MINISPDLOG_BUILD_PERF_SMOKE)
    add_executable(minispdlog_perf_smoke performance_smoke.cpp)
    target_include_directories(minispdlog_perf_smoke PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(minispdlog_perf_smoke PRIVATE minispdlog Threads::Threads)
    add_test(NAME minispdlog.performance_smoke COMMAND $<TARGET_FILE:minispdlog_perf_smoke>)
    set_tests_properties(minispdlog.performance_smoke PROPERTIES
        LABELS "performance;smoke"
        TIMEOUT 120
    )
endif()

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
# 旧测试文件保留清单（历史 API 与对比基准，不纳入统一测试目标）
# ============================================================
# 以下文件由 unit/*.cpp 中的稳定用例接替，但仍保留在仓库中：
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
