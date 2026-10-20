# 测试指南

minispdlog 使用仓库内的 doctest 单头文件作为测试内核，并把 `tests/unit/*.cpp` 只编译、链接为一个 `minispdlog_tests` 可执行文件。CTest 再按 15 个源文件注册组件条目，因此既避免重复编译，又能并行运行并把失败定位到具体组件。

## 运行全部测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --target minispdlog_tests
ctest --test-dir build -C Debug --output-on-failure
```

按 CTest 标签选择组件时可使用：

```bash
ctest --test-dir build -N
ctest --test-dir build -L async --output-on-failure
ctest --test-dir build -L filesystem --output-on-failure
```

也可以直接执行测试程序。单配置生成器通常位于：

```bash
./build/tests/minispdlog_tests
```

Visual Studio 等多配置生成器通常位于：

```powershell
.\build\tests\Debug\minispdlog_tests.exe
```

以构建系统实际输出路径为准。

## 列表与过滤

测试标签是用例名的一部分，例如 `[async]`、`[json]`、`[buffer]`、`[recovery]`、`[network]`、`[thread]`。doctest 使用通配符过滤：

```bash
# 列出用例
./build/tests/minispdlog_tests --list-test-cases

# 只运行名字中含 json 标签的用例
./build/tests/minispdlog_tests --test-case="*[json]*"

# 简写；在 PowerShell 中给整个参数加引号
./build/tests/minispdlog_tests "-tc=*[async]*"

# 查看完整参数
./build/tests/minispdlog_tests --help
```

过滤器匹配的是完整用例名。修改并发、文件或恢复代码时，不要只跑单一标签；相关逻辑往往跨多个组件，最终仍需运行全部 CTest。

## 当前测试层次

| 层次 | 位置 | 覆盖目标 |
|------|------|----------|
| 组件单测 | `tests/unit/` | level、formatter、logger、registry、队列、Sink |
| 并发与压力用例 | 带 `[thread]` / `[stress]` 的用例 | 多生产线程、串行化、突发写入 |
| 恢复用例 | `test_buffered_file.cpp`、`test_json.cpp` | WAL 回放、断尾、批次阈值、显式转储 |
| 平台用例 | `test_network.cpp`、`test_qt_sink.cpp` | Linux Socket；Qt 启用/禁用边界 |
| 性能烟雾测量 | `performance_smoke.cpp` | 同步与异步固定消息量的耗时和吞吐量，不设置机器相关硬阈值 |
| 微基准 | `benchmark_async.cpp`、`benchmark_queue.cpp` | 非正确性门禁，按需构建 |

测试应断言可观察行为，而不是只验证“没有崩溃”。例如异步测试应在 `flush()` 后检查消息内容或计数，恢复测试应重新打开文件并检查完整记录边界。

性能烟雾测量默认关闭，可独立启用并通过 CTest 的 `performance` 标签运行：

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DMINISPDLOG_BUILD_PERF_SMOKE=ON
cmake --build build-perf --config Release
ctest --test-dir build-perf -C Release -L performance --output-on-failure
```

## 平台与可选功能

网络实现只在 Linux 可用。Linux 用例通过回环 Socket 验证 UDP/TCP；其他平台应验证构造失败等公开边界，不应伪造网络成功。

Qt 测试只有 `MINISPDLOG_WITH_QT=ON` 且 CMake 找到 Qt Widgets 时才链接真实 Qt 目标：

```bash
cmake -S . -B build-qt -DMINISPDLOG_WITH_QT=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-qt --target minispdlog_tests
ctest --test-dir build-qt --output-on-failure
```

CI 或无显示器环境由测试配置使用 offscreen 平台；启用 Qt 仍需要实际 Qt SDK。

## 消毒器构建

根 CMake 为非 MSVC 编译器提供以下自定义配置：

- `DebugASan` / `RelASan`：AddressSanitizer + UndefinedBehaviorSanitizer；
- `DebugTSan` / `RelTSan`：ThreadSanitizer。

单配置生成器示例：

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=DebugASan
cmake --build build-asan --target minispdlog_tests
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=DebugTSan
cmake --build build-tsan --target minispdlog_tests
ctest --test-dir build-tsan --output-on-failure
```

ASan 与 TSan 不应混在同一进程。某些平台或编译器不提供 TSan，应把“不支持”与“测试失败”区分报告。

## 覆盖率

GCC/Clang 下可以启用覆盖率：

```bash
cmake -S . -B build-coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMINISPDLOG_ENABLE_COVERAGE=ON
cmake --build build-coverage --target coverage
```

`coverage` 目标要求本机同时存在 `lcov`、`genhtml` 和可执行的 Bash；缺少工具时 CMake 不创建该目标。覆盖率用于发现未执行分支，不等同于正确性证明，也不应为追求数字而删除有意义的断言。

## 编写新测试

1. 把用例放到对应的 `tests/unit/test_*.cpp`；新增源文件时加入 `tests/CMakeLists.txt` 的 `UNIT_TEST_SOURCES`。
2. 名称说明行为并添加组件标签，例如 `TEST_CASE("discard_new counts rejected records [async][queue]")`。
3. 文件测试使用唯一临时路径，并通过 fixture 清理自己创建的文件与 WAL。
4. 并发测试建立确定的同步边界，避免依靠固定 `sleep` 猜测工作线程状态。
5. 异步测试结束前调用 `flush()` / `shutdown()`，并恢复 registry，避免状态泄漏到下一用例。
6. 错误路径既检查异常类型，也检查资源仍可安全析构。

新增性能变化时，正确性测试与基准要分开：测试给出 pass/fail，基准只记录测量结果。具体方法见[性能方法](performance.md)。
