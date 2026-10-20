#pragma once

#include "common.h"
#include "logger.h"

#include <coroutine>
#include <functional>
#include <memory>

namespace minispdlog {
namespace coro {

    /**
     * @brief 安全请求恢复协程的可复制回调。
     *
     * 同一回调可被调用多次，底层 continuation 最多只会恢复一次。
     */
    using resume_callback = std::function<void()>;

    /**
     * @brief 将恢复回调投递回调用方执行器的调度函数。
     *
     * 调度成功返回后，必须最终至少调用一次传入的回调。如果调度器抛出异常，
     * minispdlog 会自行请求恢复，并在 `co_await` 恢复点重新抛出该异常。
     * 即使调度器在入队回调后才抛出，已入队的回调之后也只会安全地空操作。
     */
    using resume_scheduler = std::function<void(resume_callback)>;

    /**
 * @brief 在共享控制执行器上运行阻塞日志生命周期操作的 Awaitable。
 *
 * 该类型只用于低频 `flush` / `shutdown` 控制面，不参与逐条日志热路径。
 * 操作一旦提交便会执行到完成；调用方不得在挂起期间销毁所属协程。
 */
    class MINISPDLOG_API control_awaitable {
    public:
        control_awaitable(control_awaitable&&) noexcept;
        control_awaitable& operator=(control_awaitable&&) noexcept;
        control_awaitable(const control_awaitable&) = delete;
        control_awaitable& operator=(const control_awaitable&) = delete;
        ~control_awaitable();

        /** @brief 控制操作始终切换到后台执行，因此返回 false。 */
        [[nodiscard]] bool await_ready() const noexcept;
        /** @brief 保存 continuation 并将控制操作提交到共享 FIFO 执行器。 */
        void await_suspend(std::coroutine_handle<> continuation);
        /** @brief 操作失败时在恢复点重新抛出原异常。 */
        void await_resume();

    private:
        struct state;

        explicit control_awaitable(std::shared_ptr<state> operation_state) noexcept;

        std::shared_ptr<state> state_;

        friend control_awaitable flush(std::shared_ptr<logger>, resume_scheduler);
        friend control_awaitable flush_all(resume_scheduler);
        friend control_awaitable shutdown(resume_scheduler);
    };

    /**
 * @brief 非阻塞地等待一个日志器完成刷新。
 * @param target 需要保活并刷新的日志器；空指针会在恢复点抛出异常。
 * @param resume_on 可选的 continuation 调度器。
 * @return 可直接用于 `co_await` 的控制操作。
 */
    [[nodiscard]] MINISPDLOG_API control_awaitable flush(std::shared_ptr<logger> target,
                                                         resume_scheduler resume_on = {});

    /**
 * @brief 非阻塞地等待注册表中所有日志器完成刷新。
 * @param resume_on 可选的 continuation 调度器。
 */
    [[nodiscard]] MINISPDLOG_API control_awaitable flush_all(resume_scheduler resume_on = {});

    /**
 * @brief 非阻塞地执行全局排空、线程池停止与注册表清理。
 * @param resume_on 可选的 continuation 调度器。
 * @note 关闭流程一旦提交不可取消；调用方应先停止新的业务日志生产。
 */
    [[nodiscard]] MINISPDLOG_API control_awaitable shutdown(resume_scheduler resume_on = {});

} // coro
} // minispdlog
