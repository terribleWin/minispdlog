# 收集所有源文件
set(MINISPDLOG_SOURCES
    level.cpp
    pattern_formatter.cpp
    logger.cpp
    details/utils.cpp
    details/thread_pool.cpp
    registry.cpp
    formatter.cpp
)

# 创建静态库
add_library(minispdlog STATIC ${MINISPDLOG_SOURCES})

# 包含目录
target_include_directories(minispdlog PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 链接 fmt 库
target_link_libraries(minispdlog PUBLIC fmt::fmt)

# 设置编译特性
set_target_properties(minispdlog PROPERTIES 
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)