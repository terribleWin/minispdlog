# 协程控制面

minispdlog 的逐条日志数据面仍采用同步直写或“有界队列 + 工作线程”。协程接口只服务低频生命周期操作，让事件循环能够等待刷新或关闭，而不在当前线程执行阻塞的 Sink I/O。

## 快速使用

```cpp
#include <minispdlog/async.h>
#include <minispdlog/coro.h>

task<void> finish_request(std::shared_ptr<minispdlog::logger> log) {
    log->info("request finished");
    co_await minispdlog::coro::flush(std::move(log));
}
```

`task<void>` 由应用所使用的协程运行时提供。minispdlog 公开的是 Awaitable，不绑定 Asio、Qt 或某一种任务类型。

## 执行模型

```text
业务协程
  │ co_await flush / flush_all / shutdown
  ▼
共享单线程 FIFO 控制执行器
  │ 调用既有阻塞生命周期 API
  ▼
完成后恢复 continuation
```

- 每条 `info()` / `log()` 不创建协程帧，原有热路径保持不变。
- 控制执行器惰性启动，串行执行请求，因此 `flush` 后提交的 `shutdown` 有确定次序。
- `flush(shared_ptr<logger>)` 在挂起期间持有 logger 所有权。
- 操作异常会在 `co_await` 恢复点重新抛出。
- 操作提交后不可取消；尤其不能在 WAL 或 `fsync` 已开始后只完成一半。

## 回到业务执行器

标准 C++20 没有统一的 Executor，也无法自动知道协程原来属于哪个事件循环。默认 continuation 在 minispdlog 控制线程恢复。需要返回业务线程时传入调度函数：

```cpp
co_await minispdlog::coro::flush(log, [&](minispdlog::coro::resume_callback resume) {
    event_loop.post(std::move(resume));
});
```

调度函数成功返回后，必须最终至少调用一次 `resume` 回调。回调本身是幂等的：即使调度器重复调用，底层协程也最多恢复一次。

如果调度器抛出异常，minispdlog 会在控制线程发出兜底恢复请求，并在 `co_await` 恢复点重新抛出。调度器即使已将回调入队后才抛出，队列中的回调之后也只会安全地空操作，不会二次恢复或访问已销毁的协程帧。

## API 语义

| 接口 | 完成条件 |
|---|---|
| `coro::flush(logger)` | 目标 logger 的既有 `flush()` 返回 |
| `coro::flush_all()` | registry 当前快照中的 logger 全部刷新完成 |
| `coro::shutdown()` | registry 刷新、全局异步线程池排空并停止、registry 清理完成 |

`coro::flush(logger)` 只在后台操作执行期间保持 logger 所有权。在恢复 continuation 之前，内部操作会先释放对 logger 的捕获；因此当 `co_await` 继续执行时，不会因控制执行器的尾部收尾而额外延长 logger 生命周期。

控制执行器只有一个线程。如果某个自定义 Sink 永久阻塞在 `flush()`，它也会阻塞后续控制任务；库不会强行中断可能正在进行的耐久写入。应用应为外部网络或设备 Sink 配置自身超时。

## 不解决的问题

- 协程接口不会让 `fwrite`、`fsync` 或同步 Socket 自动变成内核异步 I/O。
- `async_overflow_policy::block` 在普通日志调用中仍可能阻塞生产线程。
- 不应在崩溃信号处理器、全局对象析构尾声或已被销毁的协程中启动控制操作。

队列过载策略和刷新边界见[同步与异步模型](sync-async-model.md)，落盘事务见[耐久性与恢复](durability-and-recovery.md)。
