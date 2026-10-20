# minispdlog 文档中心

这里记录 minispdlog 当前实现的使用方式、运行语义与设计边界。文档与 `0.1.0` 代码同步；如果文字与实现冲突，以 `include/minispdlog/` 和测试为准，并欢迎修正文档。

```text
应用调用 ──┬── 同步 logger ──────────────┐
           └── 异步 logger ─► 有界队列 ─┤
                                          ▼
                            formatter ─► sink ─► 终端 / 文件 / 回调 / 网络 / Qt

协程控制 ─────► FIFO 控制执行器 ─────────► flush / shutdown
```

## 按阅读目标选择

| 目标 | 文档 | 读完能解决什么 |
|------|------|----------------|
| 第一次使用 | [快速开始](getting-started.md) | 构建、链接、同步与异步示例 |
| 理解并发路径 | [同步与异步模型](sync-async-model.md) | 队列后端、过载、刷新和关闭 |
| 在协程中等待控制操作 | [协程控制面](coroutines.md) | 可等待刷新、关闭与恢复调度器 |
| 选择输出端 | [Sink 指南](sinks.md) | 各 Sink 的线程、平台与持久性差异 |
| 写可采集日志 | [结构化日志](structured-logging.md) | JSON Lines 字段、资源属性与转义 |
| 理解落盘保证 | [耐久性与恢复](durability-and-recovery.md) | 双缓冲、WAL、断尾修复与限制 |
| 查接口 | [API 速查](api.md) | 头文件、工厂、配置项与 pattern |
| 维护与扩展 | [设计决策](design-decisions.md) | 分层、不变量和扩展约束 |
| 理解项目边界 | [项目边界](project-boundaries.md) | 能力范围、接入契约与演进原则 |
| 验证改动 | [测试指南](testing.md) | CTest、过滤、消毒器与覆盖率 |
| 做性能工作 | [性能方法](performance.md) | 可复现基准、指标拆分与解读 |

更完整的逐层讲解见[项目说明](../explanation.md)，架构图源文件见 [`architecture.puml`](architecture.puml)。[`roadmap_and_testing_framework.md`](roadmap_and_testing_framework.md) 是历史规划档案，不代表当前待办或当前构建事实。

## 项目当前边界

- 核心要求 C++20，使用仓库内的 `third_party/fmt`。
- 同步 logger 与异步 logger 同时保留；异步不是启用后的唯一入口。
- 默认异步后端是阻塞 MPMC；另有单消费者的无锁 MPSC 后端。
- 协程 Awaitable 只处理低频刷新与关闭，不进入逐条日志热路径。
- 批量文件与 JSON 文件可启用 WAL 恢复和断尾修复。
- 网络 Sink 当前只在 Linux 使用真实内核 Socket；其他平台构造时抛出异常。
- Qt Sink 是可选功能，只有配置成功并定义 `MINISPDLOG_WITH_QT` 时才可包含对应头文件。
- API 版本为 `0.1.0`，公开接口、配置格式和生命周期语义仍可能演进。

## 推荐阅读路线

应用接入者：快速开始 → Sink 指南 → 同步与异步模型 → 耐久性与恢复。

日志平台接入者：结构化日志 → Sink 指南中的网络部分 → 耐久性与恢复。

贡献者：设计决策 → 测试指南 → 性能方法 → `architecture.puml`。

## 生成 API 页面

仓库根目录的 [`Doxyfile`](../Doxyfile) 可生成带中文说明的 HTML：

```bash
doxygen Doxyfile
```

输出位于 `build/docs/doxygen/html/`。Doxygen 不是构建核心库所必需的依赖。

返回[项目首页](../README.md)。
