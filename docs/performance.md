# 性能验证方法

本项目不在文档中承诺脱离硬件、编译器和 Sink 的固定吞吐数字。日志性能至少包含参数展开、记录搬运、最终格式化、队列等待、锁竞争、系统调用和存储设备延迟；只给一个 `msg/s` 会隐藏真正瓶颈。

## 先定义要测的量

| 指标 | 起点与终点 | 回答的问题 |
|------|------------|------------|
| 禁用路径成本 | 进入日志调用 → 级别过滤返回 | 关闭某级别的调用代价 |
| formatter 成本 | payload 已生成 → 文本/JSON 字节完成 | pattern 或 JSON 编码代价 |
| 入队延迟 | 进入异步调用 → 消息进入队列 | 业务线程可见延迟 |
| 端到端延迟 | 进入调用 → Sink 可观察到记录 | 真正交付延迟 |
| 排空时间 | 停止生产 → `flush()` 返回 | 突发后的积压成本 |
| 丢失率 | 产生数量与写出数量/计数差 | 过载策略的业务影响 |
| 文件提交成本 | 批次开始 → 对应 durability 完成 | `fwrite` / `fflush` / `fsync` 差异 |

异步“入队吞吐”不能与同步“已写入吞吐”直接比较。报告异步结果时至少同时给出入队时间、排空时间、队列大小、过载策略和丢弃/覆盖计数。

## 无外部依赖的性能烟雾测量

需要先确认关键路径能够执行并完成消息核算时，可启用轻量目标：

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DMINISPDLOG_BUILD_PERF_SMOKE=ON
cmake --build build-perf --config Release --target minispdlog_perf_smoke
ctest --test-dir build-perf -C Release -L performance --output-on-failure
```

它以固定消息量报告同步 `null_sink`、阻塞异步队列和无锁异步队列的 `ns/msg` 与 `msg/s`，同时覆盖单生产者和 4 生产者公开 `async_logger` 路径，并验证每组最终字节数。该程序不设置硬性能阈值，输出适合发现数量级回退，不适合代替严谨基准或跨机器排名。

## 仓库内的 Google Benchmark 目标

`tests/CMakeLists.txt` 会静默查找系统安装的 Google Benchmark。找到后生成两个可执行目标：

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --config Release \
  --target minispdlog_bench_async minispdlog_bench_queue
```

运行路径取决于生成器，单配置构建通常为：

```bash
./build-bench/tests/minispdlog_bench_async --benchmark_repetitions=5
./build-bench/tests/minispdlog_bench_queue --benchmark_repetitions=5
```

`minispdlog_bench_async` 分开测量禁用 logger、Sink 过滤、默认 pattern、`%v`、JSON、空设备文件以及异步入队。`null_sink` 仍会执行 formatter 并累计字节，不是“什么都不做”的基线。

`minispdlog_bench_queue` 比较 SPSC、MPSC 与互斥队列的若干微场景。它的生产/消费用例是 1P1C；不能据此直接推出多生产者业务负载的结果。

仓库还保留若干手写或对比基准源文件，但它们没有加入默认 CMake 目标。除非先审查测量边界、依赖和构建方式，不应把其历史输出当成当前项目结论。

## 可复现实验清单

每份结果至少记录：

- Git commit、是否有未提交改动；
- 操作系统、CPU、核心/线程数量、内存和存储介质；
- 编译器版本、生成器、`Release` 标志与链接方式；
- 消息长度、pattern 或 JSON 资源字段；
- logger / Sink 的 `_mt` 或 `_st` 版本；
- 生产线程数、工作线程数、队列容量与后端；
- 过载策略、`wake_batch`、`wake_interval_us`；
- 文件批次阈值、`durability`、是否启用恢复；
- 预热、重复次数、统计量以及异常值处理；
- 是否包含 `flush()`、是否写真实磁盘、是否清理页缓存。

避免在 Debug、ASan、TSan 构建中报告生产性能。这些配置用于找错误，其插桩会显著改变时序。

## 推荐实验顺序

1. 用 logger 级别 `off` 测禁用路径。
2. 用 `null_sink` 分别测默认 pattern、`%v` 与 JSON formatter。
3. 固定 formatter，比较同步与异步入队，并单独记录 drain。
4. 固定队列，改变生产线程与工作线程数量，观察尾延迟和丢失计数。
5. 使用真实目标文件，分别测 `none`、`fflush`、`fsync` 与不同批次阈值。
6. 最后在接近生产的采集链路上跑端到端测试。

每次只改变一个主要变量。若同时改变队列、Sink、formatter 和 durability，结果无法定位优化来源。

## 正确性护栏

优化前后必须保持：

- 输出记录数与过载策略一致；
- JSON 每行可解析且字段类型稳定；
- `flush()` 与 `shutdown()` 之后不遗留预期内消息；
- `overrun_count()` / `discard_count()` 与实际丢失相符；
- 恢复后不重复完整 WAL 批次，不保留可识别的断尾；
- ASan/UBSan/TSan 与常规单测通过。

若优化改变这些语义，它不是单纯的性能优化，应作为设计变更记录并更新[设计决策](design-decisions.md)。

## 如何解读结果

- 中位数描述典型情况，P95/P99 更能反映日志对请求尾延迟的影响。
- 小队列下的 `block` 可能把消费者瓶颈反馈给生产者；这不是测量噪声，而是策略语义。
- `discard_new` 的高入队吞吐若伴随大量丢弃，不代表交付能力更高。
- `overrun_oldest` 保护新消息但改变可见历史；必须同时展示覆盖数。
- 无锁队列避免特定队列锁，不表示完整日志路径无锁，也不保证在所有负载更快。
- 稳定配置下，`async_logger` 按生产者线程缓存线程池共享快照，只读取配置版本号；重配置才刷新共享所有权，避免多生产者反复争用同一引用计数。
- `/dev/null` 或 `NUL` 能隔离部分文件路径成本，但不能代表真实文件系统耐久写。

队列语义见[同步与异步模型](sync-async-model.md)，测试门禁见[测试指南](testing.md)。
