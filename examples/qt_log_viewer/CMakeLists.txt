if(NOT MINISPDLOG_WITH_QT OR NOT MINISPDLOG_QT_FOUND)
    return()
endif()

add_executable(qt_log_viewer main.cpp)
target_link_libraries(qt_log_viewer PRIVATE
    minispdlog
    ${MINISPDLOG_QT_TARGET}
)
target_compile_definitions(qt_log_viewer PRIVATE MINISPDLOG_WITH_QT=1)
set_target_properties(qt_log_viewer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
# Qt6 需要 AUTOMOC 若使用信号；本示例仅用 lambda connect，可不强制
set_target_properties(qt_log_viewer PROPERTIES AUTOMOC ON)
