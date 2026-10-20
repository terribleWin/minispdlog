# 收集所有源文件
set(MINISPDLOG_SOURCES
    level.cpp
    pattern_formatter.cpp
    logger.cpp
    details/utils.cpp
    details/thread_pool.cpp
    registry.cpp
    formatter.cpp
    rotating_file_sink.cpp
    daily_file_sink.cpp
    json_formatter.cpp
    details/durable_file.cpp
    network_sink.cpp
)

# 创建静态库
add_library(minispdlog STATIC ${MINISPDLOG_SOURCES})

# 包含目录
target_include_directories(minispdlog PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# fmt 是公开头文件依赖；线程库传递给静态库使用者。
target_link_libraries(minispdlog PUBLIC fmt::fmt Threads::Threads)

if(UNIX AND NOT APPLE)
    # Clang 不一定会为 std::atomic::wait/notify 自动链接 libatomic。
    target_link_libraries(minispdlog PUBLIC atomic)
endif()

# 设置编译特性
set_target_properties(minispdlog PROPERTIES 
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
