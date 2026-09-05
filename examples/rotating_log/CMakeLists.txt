add_executable(rotating_log_demo main.cpp)
target_link_libraries(rotating_log_demo PRIVATE minispdlog)
set_target_properties(rotating_log_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
# 在构建目录旁生成 logs/，便于直接查看
set_target_properties(rotating_log_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)
