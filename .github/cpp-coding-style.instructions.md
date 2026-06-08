---
applyTo: "**/*.cpp,**/*.h"
description: "C++20 工业级编码风格与导师指导规范 — minispdlog"
---

# C++ 工业级编码风格与教学指导

**C++ 标准**：项目实际使用 **C++20** (`CMAKE_CXX_STANDARD 20`)。所有示例、建议和编译命令基于此标准。代码示例实际以 C++20 特性为主， 兼容 C++17（如 `std::string_view` 替代 `const std::string&`）。

---

## 1. 命名与风格规范

优先参考 Google C++ Style Guide（截至 2024-01-01）的命名、文件组织、注释章节。本规则冲突时以本文件为准。

### 1.1 命名规则

| 类别 | 规范 | 示例 |
|------|------|------|
| 类 / 结构体 | PascalCase | `RingBuffer`, `LogMessage` |
| 函数 / 方法 | camelCase | `getLevel()`, `shouldLog()` |
| 私有成员变量 | camelCase + `_` 后缀 | `count_`, `bufferSize_` |
| 局部变量 | camelCase | `tempStr`, `itemCount` |
| 常量 / 枚举值 | `k` 前缀 + PascalCase | `kMaxQueueSize`, `kInfo`（注1） |
| 宏 | UPPER_SNAKE_CASE | `LOG_INFO`, `MINISPDLOG_API` |
| 模板参数 | PascalCase 或单大写字母 | `typename T`, `typename Allocator` |
| 命名空间 | 全小写，无层级缩略 | `minispdlog`, `minispdlog::sinks` |

> **注1**：传统 C 风格枚举（C-style enum）可适度使用全大写如 `TRACE`, `DEBUG`，但新代码优先使用 `enum class` + `kPrefix`。

### 1.2 文件组织
- 每个 public 类一个独立头文件，置于 `include/minispdlog/` 下
- 内部实现细节放 `include/minispdlog/details/`，不对外暴露
- `.cpp` 实现文件与对应头文件保持相同 basename，放 `src/` 下
- 测试文件放 `tests/`，以 `test_<模块>.cpp` 命名
- 尽量减少 `#include` 依赖，能用前向声明就不要 include

### 1.3 注释规范
- **公开接口**：必须用 `///` 或 `/** */` 写 Doxygen 风格注释，说明"做什么"+"为什么调用者需要关心"
- **实现注释**：解释"为什么这样实现"而非"代码在做什么"——代码本身已经说明"做什么"
- **TODO 注释**：格式 `// TODO(用户名): 日期 — 描述`，附 issue 或需求上下文
- 不要写显而易见的注释，如 `// 设置计数器` → 不如把变量改名为 `setCounter()`

---

## 2. 工业级编码实践（融入每课教学）

### 2.1 错误处理策略
- **API 边界**：日志库是基础组件，优先 `noexcept` + 错误码/忽略策略，而非异常（防止日志过程中的异常递归）
- **内部不变式**：用 `assert` 检查，release 编译中剥离
- **外部输入**：对用户传入的非法参数（如 nullptr pattern），采取"宽松验证 + 静默降级"（而非 crash）
- **资源安全**：始终使用 RAII 包裹资源（mutex、file handle、memory）

### 2.2 类型安全与 const 正确性
- 所有不变参数/方法标记 `const`，成员函数不修改状态时加 `const` 尾缀
- 使用 `constexpr` 代替宏定义常量和简单函数
- 优先 `enum class` 而非 `enum`，避免隐式整型转换
- 字符串参数尽量传 `string_view` 而非 `const string&`（C++17 起可用，本项目通过 type alias `string_view_t` 使用）

### 2.3 现代 C++ 特性运用
- 优先 `std::unique_ptr` 表示独有所有权，`std::shared_ptr` 仅在真正共享时使用
- 使用 `auto` 减少冗余类型书写，但不滥用（当类型对可读性至关重要时显式写出）
- 需要多态时使用 `override` 关键字，不用 `virtual` 重复标记
- 对非多态基类，考虑 `final` 阻止意外继承
- 能用 `std::array` 就不用 C 数组

### 2.4 性能意识编码
- 紧耦合热路径：避免 `std::function` 类型擦除开销，优先模板或虚函数接口
- 内存分配：明确区分"每条日志分配一次"（不可避免）和"每次格式化分配多次"（可优化）
- 避免在日志路径中使用 `std::ostringstream`，优先 `fmt::memory_buffer`（栈分配）
- 多线程环境：锁粒度尽量小，优先 `std::lock_guard` 并显式限定作用域
- 移动语义：为大型对象提供移动构造/赋值，标记 `noexcept` 以启用优化

### 2.5 可测试性设计
- 依赖接口而非具体类（如 sink 依赖 `sink` 抽象而非 `console_sink`）
- 避免静态/全局状态，或提供测试用的 reset 方法
- 耗时操作（如文件写入）在单元测试中可 mock

---

## 3. 代码审查要点（Review Checklist）

当审查用户代码时，依次检查：

1. **线程安全**：共享状态有无竞态？锁粒度是否合理？有无死锁可能？
2. **资源管理**：每条路径（包括异常路径）是否释放资源？RAII 是否完备？
3. **异常安全**：基本保证还是强保证？日志库内部是否抛异常？
4. **性能**：有无不必要的拷贝、分配、虚函数调用？
5. **接口设计**：API 是否直观？是否暴露了不必要的实现细节？
6. **边界条件**：空字符串、最大值、并发极限、文件满等场景是否覆盖？
7. **可读性**：命名是否自文档？注释是否解释了"为什么"而非"是什么"？

---

## 4. 异步日志教学要求（模块专属）

讲解异步日志模块时，按以下顺序输出：

1. **关键概念**：环形缓冲、backpressure、无锁 vs 有锁、SPSC/MPSC/MPMC
2. **方案对比**：至少两种实现思路（如基于 `std::queue` + mutex 与无锁 ring buffer），分析各自的延迟特征和适用场景
3. **锁粒度分析**：不同锁粒度对吞吐/延迟的定性影响，辅以简单的数学模型或 benchmark 结果
4. **最佳实践**：包括背压策略（丢弃 vs 阻塞 vs 降级）、批量 flush 时机
5. **代码骨架**（不超过 10 行，使用 C++20 特性，如 `std::atomic`、`std::latch` 等）

---

## 5. 边界场景处理
- 若用户提出与编码风格无关的 C++ 问题：先简要回答，然后引导回规范讨论
- 若 Google 指南与本提示规则冲突：以本提示规则优先
- 若用户坚持使用不符合本规范的模式：指出风险和理由，但尊重其选择