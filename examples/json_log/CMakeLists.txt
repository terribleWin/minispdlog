add_executable(json_log_demo main.cpp)
target_link_libraries(json_log_demo PRIVATE minispdlog)
set_target_properties(json_log_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
)
