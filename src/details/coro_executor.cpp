#include "minispdlog/coro.h"

#include "minispdlog/async.h"
#include "minispdlog/registry.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace minispdlog {
namespace coro {
    namespace {

        /** @brief 串行执行阻塞控制操作，避免占用业务事件循环线程。 */
        class control_executor {
        public:
            control_executor() {
                /// 固定静态对象析构顺序：执行器必须先于 registry 停止。
                static_cast<void>(registry::instance());
                worker_ = std::thread([this] { run_(); });
            }

            control_executor(const control_executor&) = delete;
            control_executor& operator=(const control_executor&) = delete;

            ~control_executor() {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stopping_ = true;
                }
                ready_.notify_one();
                if (worker_.joinable()) {
                    worker_.join();
                }
            }

            void post(std::function<void()> operation) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stopping_) {
                        throw std::runtime_error("minispdlog coroutine control executor is stopping");
                    }
                    operations_.push_back(std::move(operation));
                }
                ready_.notify_one();
            }

        private:
            void run_() noexcept {
                for (;;) {
                    std::function<void()> operation;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        ready_.wait(lock, [this] { return stopping_ || !operations_.empty(); });
                        if (stopping_ && operations_.empty()) {
                            return;
                        }
                        operation = std::move(operations_.front());
                        operations_.pop_front();
                    }
                    try {
                        operation();
                    } catch (...) {
                        /// control_awaitable 自己捕获操作异常；执行器不得因用户任务退出。
                    }
                }
            }

            std::mutex mutex_;
            std::condition_variable ready_;
            std::deque<std::function<void()>> operations_;
            std::thread worker_;
            bool stopping_{false};
        };

        control_executor& shared_executor() {
            static control_executor executor;
            return executor;
        }

        /** @brief 通过原子门控保证 continuation 最多恢复一次。 */
        class resume_gate {
        public:
            explicit resume_gate(std::coroutine_handle<> continuation) noexcept : continuation_(continuation) {}

            /** @brief 记录调度器或异常兜底发出的恢复请求。 */
            void request_resume() noexcept { signal_(requested); }

            /** @brief 确认调度调用已返回，此后才允许真正恢复。 */
            void arm() noexcept { signal_(armed); }

        private:
            static constexpr unsigned requested = 1U;
            static constexpr unsigned armed = 2U;
            static constexpr unsigned resumed = 4U;
            static constexpr unsigned ready = requested | armed;

            void signal_(unsigned signal) noexcept {
                auto observed = flags_.fetch_or(signal, std::memory_order_acq_rel) | signal;
                while ((observed & ready) == ready && (observed & resumed) == 0U) {
                    if (flags_.compare_exchange_weak(observed, observed | resumed, std::memory_order_acq_rel,
                                                     std::memory_order_acquire)) {
                        continuation_.resume();
                        return;
                    }
                }
            }

            std::atomic<unsigned> flags_{0U};
            std::coroutine_handle<> continuation_;
        };

    } // namespace

    struct control_awaitable::state {
        std::function<void()> operation;
        resume_scheduler resume_on;
        std::coroutine_handle<> continuation{};
        std::exception_ptr error;
        bool submitted{false};

        void run() noexcept {
            auto pending_operation = std::move(operation);
            try {
                pending_operation();
            } catch (...) {
                error = std::current_exception();
            }
            /// 在恢复 continuation 之前释放 logger 等操作捕获对象。
            pending_operation = {};

            if (!resume_on) {
                continuation.resume();
                return;
            }

            auto scheduler = std::move(resume_on);
            try {
                auto gate = std::make_shared<resume_gate>(continuation);
                resume_callback request_resume = [gate]() noexcept { gate->request_resume(); };
                try {
                    scheduler(request_resume);
                } catch (...) {
                    if (!error) {
                        error = std::current_exception();
                    }
                    /// 可能已入队的回调与兜底共用门控，不会重复恢复。
                    request_resume();
                }
                /// 先释放调度器捕获，再允许 continuation 恢复。
                scheduler = {};
                gate->arm();
            } catch (...) {
                /// 门控或回调分配在调度器被调用前失败，可安全原地恢复。
                if (!error) {
                    error = std::current_exception();
                }
                scheduler = {};
                continuation.resume();
            }
        }
    };

    control_awaitable::control_awaitable(std::shared_ptr<state> operation_state) noexcept
        : state_(std::move(operation_state)) {}

    control_awaitable::control_awaitable(control_awaitable&&) noexcept = default;
    control_awaitable& control_awaitable::operator=(control_awaitable&&) noexcept = default;
    control_awaitable::~control_awaitable() = default;

    bool control_awaitable::await_ready() const noexcept {
        return false;
    }

    void control_awaitable::await_suspend(std::coroutine_handle<> continuation) {
        if (!state_) {
            throw std::logic_error("empty minispdlog control awaitable");
        }
        if (state_->submitted) {
            throw std::logic_error("minispdlog control awaitable can only be awaited once");
        }
        state_->submitted = true;
        state_->continuation = continuation;
        auto operation_state = state_;
        /// 提交必须是最后一步；post 成功后 continuation 可能立即在另一线程恢复。
        shared_executor().post([operation_state] { operation_state->run(); });
    }

    void control_awaitable::await_resume() {
        if (!state_) {
            throw std::logic_error("empty minispdlog control awaitable");
        }
        auto error = state_->error;
        state_.reset();
        if (error) {
            std::rethrow_exception(error);
        }
    }

    control_awaitable flush(std::shared_ptr<logger> target, resume_scheduler resume_on) {
        auto operation_state = std::make_shared<control_awaitable::state>();
        operation_state->operation = [target = std::move(target)] {
            if (!target) {
                throw std::invalid_argument("minispdlog::coro::flush requires a logger");
            }
            target->flush();
        };
        operation_state->resume_on = std::move(resume_on);
        return control_awaitable(std::move(operation_state));
    }

    control_awaitable flush_all(resume_scheduler resume_on) {
        auto operation_state = std::make_shared<control_awaitable::state>();
        operation_state->operation = [] { registry::instance().flush_all(); };
        operation_state->resume_on = std::move(resume_on);
        return control_awaitable(std::move(operation_state));
    }

    control_awaitable shutdown(resume_scheduler resume_on) {
        auto operation_state = std::make_shared<control_awaitable::state>();
        operation_state->operation = [] { minispdlog::shutdown(); };
        operation_state->resume_on = std::move(resume_on);
        return control_awaitable(std::move(operation_state));
    }

} // coro
} // minispdlog
