#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "framework/doctest.h"

/**
 * @file test_main.cpp
 * @brief minispdlog 单元测试的统一 doctest 入口。
 *
 * 所有组件共用一个可执行文件，CTest 通过 doctest 的源文件过滤器
 * 将它分成独立条目，因而既避免重复编译，又能快速定位失败组件。
 */
