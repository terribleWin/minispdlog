cmake_minimum_required(VERSION 3.11)
project(minispdlog VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # ← 改这里：加上 _REQUIRED
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)   # ← 顺便修了：Wpendatic → Wpedantic
endif()

# ============================================================
# 构建变体（继承 CS144 Sponge 设计）
# ============================================================
# Debug:     调试符号 + 低优化
# Release:   全优化（默认）
# RelASan:   release + AddressSanitizer + UBSan
# RelTSan:   release + ThreadSanitizer
# DebugASan: debug + AddressSanitizer + UBSan
# DebugTSan: debug + ThreadSanitizer
# ============================================================

set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelASan;RelTSan;DebugASan;DebugTSan")

if(NOT MSVC)
    # ASan + UBSan 标志
    set(ASAN_FLAGS "-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer")
    # TSan 标志
    set(TSAN_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")

    # RelASan: Release + ASan + UBSan（内存和 UB 检测，接近生产性能）
    set(CMAKE_CXX_FLAGS_RELASAN "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g ${ASAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELASAN "${ASAN_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_RELASAN "${ASAN_FLAGS}")

    # RelTSan: Release + TSan（线程竞争检测，接近生产性能）
    set(CMAKE_CXX_FLAGS_RELTSAN "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g ${TSAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELTSAN "${TSAN_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_RELTSAN "${TSAN_FLAGS}")

    # DebugASan: Debug + ASan + UBSan（开发调试最常用）
    set(CMAKE_CXX_FLAGS_DEBUGASAN "${CMAKE_CXX_FLAGS_DEBUG} ${ASAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUGASAN "${ASAN_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUGASAN "${ASAN_FLAGS}")

    # DebugTSan: Debug + TSan（线程竞争 + 调试符号）
    set(CMAKE_CXX_FLAGS_DEBUGTSAN "${CMAKE_CXX_FLAGS_DEBUG} ${TSAN_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUGTSAN "${TSAN_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUGTSAN "${TSAN_FLAGS}")
endif()

# ============================================================
# clang-format / clang-tidy（读取仓库根目录的 .clang-format / .clang-tidy）
#   cmake --build build --target format        # 就地格式化
#   cmake --build build --target format-check  # CI：有差异即失败
#   cmake -DCLANG_TIDY=clang-tidy ..           # 编译时跑 tidy
# ============================================================
# Allowlist matches scripts/lint.sh (files kept clean against .clang-format).
set(MINISPDLOG_FORMAT_FILES
    "${CMAKE_SOURCE_DIR}/include/minispdlog/sinks/daily_file_sink.h"
    "${CMAKE_SOURCE_DIR}/src/daily_file_sink.cpp"
    "${CMAKE_SOURCE_DIR}/tests/unit/test_daily_file.cpp"
)

find_program(CLANG_FORMAT_EXE NAMES clang-format clang-format-18 clang-format-16 clang-format-14)
if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i --style=file
                ${MINISPDLOG_FORMAT_FILES}
        COMMENT "clang-format using ${CMAKE_SOURCE_DIR}/.clang-format"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror --style=file
                ${MINISPDLOG_FORMAT_FILES}
        COMMENT "clang-format --dry-run using ${CMAKE_SOURCE_DIR}/.clang-format"
        VERBATIM
    )
endif()

if(CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY
        "${CLANG_TIDY};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy;--header-filter=include[/\\\\]minispdlog[/\\\\]")
endif()

# Release 构建默认只保留 info 及以上，trace/debug 编译期移除
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(MINISPDLOG_ACTIVE_LEVEL=MINISPDLOG_LEVEL_INFO)
endif()

# ============================================================
# 可选 Qt GUI Sink（SDK 建议装到 third_party/qt，见 scripts/setup_qt.sh）
#   cmake -DMINISPDLOG_WITH_QT=ON ..
# ============================================================
option(MINISPDLOG_WITH_QT "Build Qt GUI sink, tests and example" OFF)
set(MINISPDLOG_QT_FOUND FALSE)
if(MINISPDLOG_WITH_QT)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MinispdlogQt.cmake")
    minispdlog_find_qt(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qt")
    if(MINISPDLOG_QT_FOUND)
        message(STATUS "minispdlog: Qt ${MINISPDLOG_QT_VERSION} enabled for qt_sink")
        add_compile_definitions(MINISPDLOG_WITH_QT=1)
    else()
        message(WARNING
            "MINISPDLOG_WITH_QT=ON but Qt was not found.\n"
            "  Install into third_party/qt via: ./scripts/setup_qt.sh\n"
            "  Or install system packages (e.g. qt6-base-dev) and reconfigure.")
    endif()
endif()

add_subdirectory(third_party/fmt)
add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
add_subdirectory(examples/rotating_log)
add_subdirectory(examples/json_log)
add_subdirectory(examples/callback_log)
add_subdirectory(examples/buffered_log)
add_subdirectory(examples/color_log)

if(MINISPDLOG_WITH_QT AND MINISPDLOG_QT_FOUND)
    add_subdirectory(examples/qt_log_viewer)
endif()

# ========== 安装规则 ==========

# 安装头文件
install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING PATTERN "*.h")

# 安装 minispdlog 库（fmt 由用户自行管理）
install(TARGETS minispdlog
        EXPORT minispdlog-targets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)

# 生成 CMake 配置文件供 find_package 使用
install(EXPORT minispdlog-targets
        FILE minispdlog-config.cmake
        NAMESPACE minispdlog::
        DESTINATION lib/cmake/minispdlog)