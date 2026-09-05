# 在项目根 third_party/qt 或系统路径中查找 Qt5/Qt6 Widgets
# 用法：include(cmake/MinispdlogQt.cmake) 后调用 minispdlog_find_qt()

function(minispdlog_find_qt)
    set(options)
    set(oneValueArgs ROOT)
    set(multiValueArgs)
    cmake_parse_arguments(MQT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MQT_ROOT)
        set(MQT_ROOT "${PROJECT_SOURCE_DIR}/third_party/qt")
    endif()

    # aqtinstall 典型布局：
    #   third_party/qt/6.5.3/gcc_64
    #   third_party/qt/6.5.3/msvc2019_64
    set(_prefix_candidates "")
    if(EXISTS "${MQT_ROOT}")
        file(GLOB _qt_ver_dirs "${MQT_ROOT}/6.*")
        file(GLOB _qt5_ver_dirs "${MQT_ROOT}/5.*")
        list(APPEND _qt_ver_dirs ${_qt5_ver_dirs})
        foreach(_ver ${_qt_ver_dirs})
            foreach(_kit gcc_64 clang_64 msvc2019_64 msvc2022_64 mingw_64)
                if(EXISTS "${_ver}/${_kit}")
                    list(APPEND _prefix_candidates "${_ver}/${_kit}")
                endif()
            endforeach()
        endforeach()
    endif()

    if(_prefix_candidates)
        list(GET _prefix_candidates 0 _first)
        list(PREPEND CMAKE_PREFIX_PATH "${_first}")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
        message(STATUS "minispdlog: using Qt under ${_first}")
    else()
        message(STATUS "minispdlog: no Qt under ${MQT_ROOT}, trying system Qt")
    endif()

    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        set(MINISPDLOG_QT_TARGET Qt6::Widgets PARENT_SCOPE)
        set(MINISPDLOG_QT_FOUND TRUE PARENT_SCOPE)
        set(MINISPDLOG_QT_VERSION "${Qt6_VERSION}" PARENT_SCOPE)
        return()
    endif()

    find_package(Qt5 COMPONENTS Widgets QUIET)
    if(Qt5_FOUND)
        set(MINISPDLOG_QT_TARGET Qt5::Widgets PARENT_SCOPE)
        set(MINISPDLOG_QT_FOUND TRUE PARENT_SCOPE)
        set(MINISPDLOG_QT_VERSION "${Qt5_VERSION}" PARENT_SCOPE)
        return()
    endif()

    set(MINISPDLOG_QT_FOUND FALSE PARENT_SCOPE)
endfunction()
