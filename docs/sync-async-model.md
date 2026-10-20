# 同步与异步模型

minispdlog 把“调用是否等待 Sink I/O”作为显式选择。同步与异步共用 `logger`、formatter 和 Sink，不是两套互不兼容的日志系统。

## 两条调用链

```text
同步：调用线程
fmt 参数展开 → log_msg → Sink 级别判断 → formatter → 输出/刷新

异步：调用线程                         工作线程
fmt 参数展开 → 拥有数据的 async_msg → 有界队列 → formatter → 输出/刷新
```

`logger::info("id={}", id)` 首先在调用线程把参数展开为 payload。异步消息会拥有 payload 字节，短消息使用 256 字节内联缓冲；工作线程再按每个 Sink 的 formatter 生成最终文本或 JSON。不要把“异步”理解为调用线程完全不做格式化。

## 同步路径

普通 `logger` 的 `sink_it_()` 直接遍历当前 Sink 快照。logger 的 Sink 列表采用写时复制：记录与刷新只读取一个共享快照，`add_sink()` / `remove_sink()` 串行发布新列表，logger 不会在 Sink I/O 期间持有列表变更锁。

每个 `_mt` Sink 在自身互斥锁内完成 formatter 与输出；不同 Sink 的锁相互独立。`_st` 版本用 `null_mutex`，只适合没有并发调用的所有权模型。

## 异步线程池

所有工厂创建的 `async_logger` 使用同一个全局线程池：

```cpp
minispdlog::thread_pool_options opts;
opts.queue_size = 16384;
opts.thread_count = 2;
opts.queue_type = minispdlog::async_queue_type::blocking;
opts.wake_batch = 64;
opts.wake_interval_us = 100;
minispdlog::init_thread_pool(opts);
```

| 选项 | 默认值 | 含义 |
|------|--------|------|
| `queue_size` | 8192 | 有界队列容量，必须大于零 |
| `thread_count` | 1 | 阻塞后端的工作线程数，范围 1–1000 |
| `queue_type` | `blocking` | `blocking` MPMC 或 `lockfree` MPSC |
| `wake_batch` | 64 | 累计到该数量时唤醒休眠消费者；零按 1 处理 |
| `wake_interval_us` | 100 | 尾部消息最长的轮询等待粒度；零会转为逐条唤醒配置 |

重新调用 `init_thread_pool()` 会原子发布新池，再排空并关闭旧池。每次异步投递都会取得当前池的 `shared_ptr`，因此正在进行的投递不会因并发替换而提前失去池对象；管理器同时递增 generation 供状态识别。配置切换仍应放在受控生命周期边界，避免让正常流量承担旧池排空时间。

### 队列后端

| 后端 | 生产/消费模型 | 工作线程 | 可用过载策略 | 适合 |
|------|---------------|----------|--------------|------|
| `blocking` | MPMC，互斥锁 + 条件变量 | 配置数量 | 全部三种 | 通用、多工作线程 |
| `lockfree` | 有界 MPSC 环 | 固定为 1 | `block`、`discard_new` | 多生产者、单消费者且愿意接受满队列自旋 |

`init_lockfree_thread_pool(size)` 是无锁后端的一线程快捷入口。即便 `thread_pool_options.thread_count` 大于 1，无锁后端仍强制使用一个消费者。

“无锁”只描述 MPSC 队列本身；formatter、Sink、文件系统和调度仍可能加锁或阻塞。是否更适合某个负载必须通过该负载的基准验证。

## 队列满时的策略

| 策略 | 满队列行为 | 计数 | 注意事项 |
|------|------------|------|----------|
| `block` | 等到有空间；无锁后端通过 `yield` 重试 | 不计丢失 | 提供背压，调用延迟可能增加 |
| `overrun_oldest` | 覆盖队首最旧的日志消息 | `overrun_count()` | 仅阻塞 MPMC；`flush`、`terminate` 控制消息不会被覆盖；若队首为控制消息则拒绝新日志并计入 `discard_count()`；无锁后端抛 `std::invalid_argument` |
| `discard_new` | 丢弃本次新消息 | `discard_count()` | 调用不等待空位，但必须监控丢弃量 |

可以读取当前池的计数：

```cpp
if (auto* pool = minispdlog::get_thread_pool()) {
    auto overwritten = pool->overrun_count();
    auto discarded = pool->discard_count();
    (void)overwritten;
    (void)discarded;
}
```

计数属于线程池而非单个 logger。替换线程池会开始一组新的池级计数。`get_thread_pool()` 返回由线程局部共享快照支撑的裸指针：稳定配置下只读取版本号，不反复修改共享引用计数；重配置后才刷新快照。旧池可能已经停止，但当前快照不会悬空。若要跨线程保存，应改用 `thread_pool_manager::instance().get()` 显式持有共享所有权。

## 顺序与可见性

- 单个生产线程入队的先后关系由队列保留。
- 使用一个工作线程时，Sink 看到队列消费顺序。
- 阻塞后端配置多个工作线程时，不应依赖跨消息的全局输出顺序；并行工作线程到达同一个 Sink 锁的次序可能不同。
- 多个生产线程之间若需要业务顺序，应用应在消息中写入自己的序列号或关联标识。

## `flush()`、析构与 `shutdown()`

`async_logger::flush()` 投递一个带确认的刷新消息并等待，最后直接刷新该 logger 的 Sink。单工作线程依靠 FIFO 保证刷新消息之前的日志已经完成，不需要逐条原子计数；多工作线程还会等待并行处理中日志的计数归零。它是同步边界，不只是“请求稍后刷新”。

异步 logger 析构时，如果仍持有活动线程池，会尝试投递并等待刷新。仍建议应用显式控制顺序：

```cpp
log->flush();
minispdlog::shutdown();
```

`shutdown()` 的顺序是：

1. 对 registry 中的默认与具名 logger 执行 `flush()`；
2. 关闭全局线程池并连接工作线程；
3. 清空 registry。

线程池关闭过程中收到的新异步记录会走同步后端写入。不要把这项兜底当作常规并发关闭协议；应用应先停止产生日志的业务线程，再调用 `shutdown()`。

## 选择建议

- 希望调用返回时输出已交给 Sink：使用同步 logger。
- 需要隔离通常的 Sink 延迟：使用异步 logger，并明确过载策略。
- 不能丢记录：从 `block` 开始评估，同时监控尾延迟。
- 允许削峰丢失：选择 `discard_new` 或阻塞后端的 `overrun_oldest`，同时采集计数。
- 要求简单、稳定的输出顺序：异步池只配置一个工作线程。
- 需要进程崩溃后的文件恢复：异步队列本身不提供持久性，还需使用[批量耐久文件 Sink](durability-and-recovery.md)。

相关接口见 [API 速查](api.md)，基准方法见[性能方法](performance.md)。
