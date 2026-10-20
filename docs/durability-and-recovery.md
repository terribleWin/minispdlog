# 耐久性与恢复

本页描述 `buffered_file_sink`、`json_file_sink` 与底层 `durable_file` 的真实边界。这里的“耐久”指可配置的批量提交与恢复流程，不代表任何配置都能抵御断电、磁盘控制器缓存或文件系统故障。

## 数据路径

```text
格式化记录
    │ append
    ▼
前台缓冲区 ──达到阈值/flush──► 冻结缓冲区
    ▲                              │
    └──── 新记录继续写入新前台 ◄──┘
                                   │ WAL sidecar
                                   ▼
                              主日志文件
```

`buffered_file_sink_mt` 在 Sink 锁内追加和交换缓冲区，写冻结批次时释放该锁，生产线程可继续填充另一个前台缓冲区。同一个 writer 同时只提交一个冻结批次；显式 `flush()` 会等待进行中的提交结束。

`buffered_file_sink_st` 不提供并发保护，提交在调用线程完成。

## 批次触发条件

```cpp
minispdlog::batch_config cfg;
cfg.max_bytes = 64 * 1024;
cfg.max_records = 256;
cfg.max_age = std::chrono::milliseconds{50};
cfg.commit = minispdlog::durability::fflush;
cfg.recover = true;

auto log = minispdlog::buffered_logger_mt("durable", "logs/app.log", false, cfg);
```

字节数、记录数和驻留时间三个非零条件满足任意一个时提交。每个字段为零表示关闭对应条件。

`max_age` 没有专用定时线程：它在下一条日志到来时被检查。若流量停止，尾部批次会一直保留到显式 `flush()`、Sink 析构、`dump_buffered_logs()` 或另一次日志调用。需要确定的时间边界时，由应用定期调用 `flush()`。

`batch_config::until_flush()` 会把三个自动触发条件全部设为零。

## 提交级别

| `durability` | 每批操作 | 能说明的保证 | 不能说明的保证 |
|--------------|----------|--------------|----------------|
| `none` | 主文件执行 `fwrite` | 数据交给 C 运行库缓冲 | 进程崩溃后仍存在 |
| `fflush` | `fwrite` 后 `fflush` | 数据从 C 运行库交给操作系统 | 断电后仍在稳定介质 |
| `fsync` | `fflush` 后 `fsync` / `FlushFileBuffers` | 请求操作系统同步文件内容 | 硬件、文件系统或目录元数据在所有环境中的绝对保证 |

更强的级别通常意味着更多系统调用。应根据可丢失窗口、磁盘类型和实际部署环境选择，并用故障注入验证，而不是只看吞吐数字。

## WAL 提交流程

当 `recover=true` 时，一个冻结批次按以下顺序处理：

1. 取得主文件当前目标偏移；
2. 在主日志旁写入 sidecar WAL，内容包括 magic、版本、目标偏移、payload 长度和批次字节；
3. 把批次写入主文件，并应用所选 durability；
4. 删除 WAL。

sidecar 路径由内部 `wal_path_for()` 生成，属于实现细节，不应被业务代码当作稳定交换格式。一次 writer 只处理一个 WAL 批次，它不是通用事务日志。

## 打开时恢复

当 `truncate=false` 且 `recover=true` 时，构造 writer 会先回放有效 WAL，再修复主文件尾部：

- WAL 只采用其中完整、以换行结尾的记录前缀；
- 主文件尚未到目标偏移、刚好在目标偏移或只写入部分批次时，恢复逻辑会按当前文件状态补写或从目标偏移重写；
- 主文件已包含完整批次时不重复追加；
- 无效、未知版本或不完整的 WAL 会被移除；
- 主文件最后一行没有 `\n` 时，在尾部 256 KiB 窗口内回退到最后一个完整换行。

恢复是面向“每条记录以换行结束”的日志设计。默认 `pattern_formatter` 与 `json_formatter` 都会追加换行。自定义 formatter 若不遵循该约定，断尾修复可能删除有效数据或无法识别记录边界。单条记录超过 256 KiB 时，断尾扫描也可能找不到上一个换行，因此不应把超大 payload 与此恢复模型组合后视作完整保证。

`truncate=true` 会删除遗留 WAL 并重建主文件，不执行回放。

## 显式刷新与退出

```cpp
log->info("checkpoint={}", 7);
log->flush();           ///< 提交前台缓冲并应用 durability。
minispdlog::shutdown(); ///< 若还使用异步 logger，应在进程正常退出前调用。
```

同步 `buffered_logger_*` 的 `flush()` 直接提交。`async_buffered_file_mt` 的 `flush()` 还会等待异步队列到达刷新边界，再提交 Sink 缓冲。

析构函数会尽力刷新，但显式生命周期更容易报告错误和验证顺序。异常终止、`_Exit`、强制结束进程或机器断电不能依赖普通析构。

## 崩溃时尽力转储

`dump_buffered_logs()` 会遍历当前注册的 durable writer，把前台缓冲尽力写入并 `fflush`。`install_crash_flush()` 安装进程级 fatal handler，并保证重复安装不会重复注册。

这条路径明确是 best-effort，且实现本身标注为“非 signal-safe”：它不能替代正常 `flush()`、操作系统级崩溃收集或外部日志守护进程。应用已有信号/SEH 策略时，还应在集成环境验证处理器之间的关系。

## 使用建议

- 只需要普通即时文件输出：`basic_logger_mt`。
- 希望合并小写入，并接受一段前台缓冲窗口：`buffered_logger_mt`。
- 同时需要固定结构化字段：`json_logger_mt`，它复用同一 durable writer。
- 需要异步隔离调用线程：`async_buffered_file_mt` 或 `async_json_file_mt`；记住异步队列和文件 WAL 是两个不同层次。
- 需要按大小或日期切分：当前 rotating/daily Sink 不使用此 WAL writer；其刷新与恢复边界不同。

测试恢复逻辑的方法见[测试指南](testing.md)，Sink 全览见 [Sink 指南](sinks.md)。
