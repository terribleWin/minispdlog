// 队列微基准：SPSC / MPSC / mutex_queue 相对对比
//
// 为何以前跑很久：
//   Google Benchmark 默认每个 case 至少跑 ~0.5s，且 ProdCons 曾用 10 万消息、
//   多组 Args，总时长可达数十分钟；多生产者 + 实验性 mpsc 还可能自旋挂死。
//
// 当前策略：
//   - 缩小消息量；重负载 MinTime=0.05s + 显式 Iterations 硬封顶
//   - 去掉 SPSC 多生产者（协议不允许）
//   - 去掉 MPSC 多生产者（实现仍为实验性，忙等可能永不结束）
#include "minispdlog/details/mpsc_queue.h"
#include "minispdlog/details/spsc_queue.h"
#include <atomic>
#include <benchmark/benchmark.h>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using namespace minispdlog::details;

namespace {

constexpr double kLightMinTime = 0.1;
constexpr double kHeavyMinTime = 0.05;
// 含线程创建的 case：最多跑这么多轮，避免统计收敛拖太久
constexpr int kHeavyMaxIterations = 20;

template <typename T>
class mutex_queue {
public:
    explicit mutex_queue(size_t cap) : capacity_(cap) {}

    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.size() >= capacity_)
            return false;
        q_.push(item);
        return true;
    }
    bool push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.size() >= capacity_)
            return false;
        q_.push(std::move(item));
        return true;
    }
    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.empty())
            return false;
        item = std::move(q_.front());
        q_.pop();
        return true;
    }

private:
    size_t capacity_;
    std::queue<T> q_;
    std::mutex mutex_;
};

template <typename Q>
void drain_some(Q& q, int n) {
    int v;
    for (int i = 0; i < n; ++i) {
        if (!q.pop(v))
            break;
    }
}

} // namespace

// ==================== 1. 单线程 push ====================
template <typename Q>
void bench_push(benchmark::State& state) {
    Q q(1 << 16);
    for (auto _ : state) {
        if (!q.push(42)) {
            state.PauseTiming();
            drain_some(q, 4096);
            state.ResumeTiming();
            benchmark::DoNotOptimize(q.push(42));
        }
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_SPSC_Push(benchmark::State& state) {
    bench_push<spsc_queue<int>>(state);
}
static void BM_MPSC_Push(benchmark::State& state) {
    bench_push<mpsc_queue<int>>(state);
}
static void BM_Mutex_Push(benchmark::State& state) {
    bench_push<mutex_queue<int>>(state);
}

BENCHMARK(BM_SPSC_Push)->Unit(benchmark::kNanosecond)->MinTime(kLightMinTime);
BENCHMARK(BM_MPSC_Push)->Unit(benchmark::kNanosecond)->MinTime(kLightMinTime);
BENCHMARK(BM_Mutex_Push)->Unit(benchmark::kNanosecond)->MinTime(kLightMinTime);

// ==================== 2. 单线程 pop（按批 4096，避免「清空 10 万」假大数） ====================
template <typename Q>
void bench_pop(benchmark::State& state) {
    constexpr int kBatch = 4096;
    for (auto _ : state) {
        state.PauseTiming();
        Q q(kBatch + 8);
        for (int i = 0; i < kBatch; ++i) {
            q.push(i);
        }
        state.ResumeTiming();

        int v;
        for (int i = 0; i < kBatch; ++i) {
            benchmark::DoNotOptimize(q.pop(v));
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kBatch));
}

static void BM_SPSC_Pop(benchmark::State& state) {
    bench_pop<spsc_queue<int>>(state);
}
static void BM_MPSC_Pop(benchmark::State& state) {
    bench_pop<mpsc_queue<int>>(state);
}
static void BM_Mutex_Pop(benchmark::State& state) {
    bench_pop<mutex_queue<int>>(state);
}

BENCHMARK(BM_SPSC_Pop)->Unit(benchmark::kNanosecond)->MinTime(kHeavyMinTime);
BENCHMARK(BM_MPSC_Pop)->Unit(benchmark::kNanosecond)->MinTime(kHeavyMinTime);
BENCHMARK(BM_Mutex_Pop)->Unit(benchmark::kNanosecond)->MinTime(kHeavyMinTime);

// ==================== 3. 队列已满 push ====================
template <typename Q>
void bench_push_full(benchmark::State& state) {
    constexpr int kCap = 64;
    constexpr int kTries = 1000;
    for (auto _ : state) {
        state.PauseTiming();
        Q q(kCap);
        for (int i = 0; i < kCap; ++i) {
            q.push(i);
        }
        state.ResumeTiming();

        int failed = 0;
        for (int i = 0; i < kTries; ++i) {
            if (!q.push(i))
                ++failed;
        }
        benchmark::DoNotOptimize(failed);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kTries));
}

static void BM_SPSC_PushFull(benchmark::State& state) {
    bench_push_full<spsc_queue<int>>(state);
}
static void BM_Mutex_PushFull(benchmark::State& state) {
    bench_push_full<mutex_queue<int>>(state);
}

BENCHMARK(BM_SPSC_PushFull)->Unit(benchmark::kNanosecond)->MinTime(kHeavyMinTime);
BENCHMARK(BM_Mutex_PushFull)->Unit(benchmark::kNanosecond)->MinTime(kHeavyMinTime);

// ==================== 4. 1 生产 + 1 消费 ====================
template <typename Q>
void bench_prodcons(benchmark::State& state) {
    const int total_msgs = static_cast<int>(state.range(0));
    const int queue_size = static_cast<int>(state.range(1));

    for (auto _ : state) {
        state.PauseTiming();
        Q q(queue_size);
        std::atomic<int> consumed{0};
        state.ResumeTiming();

        std::thread consumer([&]() {
            int v;
            while (consumed.load(std::memory_order_relaxed) < total_msgs) {
                if (q.pop(v)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });

        for (int i = 0; i < total_msgs; ++i) {
            while (!q.push(i)) {
            }
        }
        consumer.join();
        benchmark::DoNotOptimize(consumed.load());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_SPSC_ProdCons(benchmark::State& state) {
    bench_prodcons<spsc_queue<int>>(state);
}
static void BM_MPSC_ProdCons(benchmark::State& state) {
    bench_prodcons<mpsc_queue<int>>(state);
}
static void BM_Mutex_ProdCons(benchmark::State& state) {
    bench_prodcons<mutex_queue<int>>(state);
}

BENCHMARK(BM_SPSC_ProdCons)
    ->Args({2000, 256})
    ->Args({5000, 1024})
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(kHeavyMaxIterations)
    ->UseRealTime();
BENCHMARK(BM_MPSC_ProdCons)
    ->Args({2000, 256})
    ->Args({5000, 1024})
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(kHeavyMaxIterations)
    ->UseRealTime();
BENCHMARK(BM_Mutex_ProdCons)
    ->Args({2000, 256})
    ->Args({5000, 1024})
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(kHeavyMaxIterations)
    ->UseRealTime();

// ==================== 5. 队列容量影响（1P1C） ====================
template <typename Q>
void bench_queue_size_impact(benchmark::State& state) {
    constexpr int kMsgs = 3000;
    const int queue_size = static_cast<int>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        Q q(queue_size);
        std::atomic<int> consumed{0};
        state.ResumeTiming();

        std::thread consumer([&]() {
            int v;
            while (consumed.load(std::memory_order_relaxed) < kMsgs) {
                if (q.pop(v)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });

        for (int i = 0; i < kMsgs; ++i) {
            while (!q.push(i)) {
            }
        }
        consumer.join();
        benchmark::DoNotOptimize(consumed.load());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kMsgs));
}

static void BM_SPSC_SizeImpact(benchmark::State& state) {
    bench_queue_size_impact<spsc_queue<int>>(state);
}
static void BM_Mutex_SizeImpact(benchmark::State& state) {
    bench_queue_size_impact<mutex_queue<int>>(state);
}

BENCHMARK(BM_SPSC_SizeImpact)
    ->Arg(64)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(kHeavyMaxIterations)
    ->UseRealTime();
BENCHMARK(BM_Mutex_SizeImpact)
    ->Arg(64)
    ->Arg(1024)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(kHeavyMaxIterations)
    ->UseRealTime();

BENCHMARK_MAIN();
