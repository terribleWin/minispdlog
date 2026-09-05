#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "framework/doctest.h"

/**
 * @brief 统一测试入口
 *
 * 这一行宏定义让 doctest 自动生成 main() 函数，支持：
 *   - 命令行过滤：./minispdlog_tests "[sink]"       只跑 sink 标签的测试
 *   - 列表测试：  ./minispdlog_tests --list-tests
 *   - 详细输出：  ./minispdlog_tests -s
 *   - 只跑失败：  ./minispdlog_tests --only-failures
 *
 * 比当前项目"每个测试文件独立编译成可执行文件"的方式：
 *   ✅ 编译速度更快（只需要编译一次 doctest main）
 *   ✅ 运行更方便（一个命令跑全部测试）
 *   ✅ 更容易集成 CTest（ctest 自动发现所有 TEST_CASE）
 */
