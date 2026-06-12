// 环形队列性能对比：无锁 SPSC vs 有锁 mutex
#include <benchmark/benchmark.h>
#include "minispdlog/details/spsc_queue.h"
#include "minispdlog/details/mpsc_queue.h"
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>

using namespace minispdlog::details;

// ==================== 有锁队列（用于对比） ====================
template<typename T>
class mutex_queue {
public:
    explicit mutex_queue(size_t cap) : capacity_(cap) {}
    bool push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.size() >= capacity_) return false;
        q_.push(item);
        return true;
    }
    bool push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.size() >= capacity_) return false;
        q_.push(std::move(item));
        return true;
    }
    bool pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.empty()) return false;
        item = std::move(q_.front());
        q_.pop();
        return true;
    }
private:
    size_t capacity_;
    std::queue<T> q_;
    std::mutex mutex_;
};

// ==================== 场景 1: 单线程纯 push ====================
template<typename Q>
void bench_push(benchmark::State& state) {
    Q q(1048576);
    for (auto _ : state) {
        q.push(42);
    }
}

static void BM_SPSC_Push(benchmark::State& state) { bench_push<spsc_queue<int>>(state); }
BENCHMARK(BM_SPSC_Push);

static void BM_Mutex_Push(benchmark::State& state) { bench_push<mutex_queue<int>>(state); }

static void BM_MPSC_Push(benchmark::State& state) { bench_push<mpsc_queue<int>>(state); }
BENCHMARK(BM_MPSC_Push);
BENCHMARK(BM_Mutex_Push);

// ==================== 场景 2: 单线程纯 pop ====================
template<typename Q>
void bench_pop(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        Q q(1048576);
        for (int i = 0; i < 100000; i++) q.push(i);
        state.ResumeTiming();
        int v;
        while (q.pop(v)) {}
    }
}

static void BM_SPSC_Pop(benchmark::State& state) { bench_pop<spsc_queue<int>>(state); }
BENCHMARK(BM_SPSC_Pop);

static void BM_Mutex_Pop(benchmark::State& state) { bench_pop<mutex_queue<int>>(state); }

static void BM_MPSC_Pop(benchmark::State& state) { bench_pop<mpsc_queue<int>>(state); }
BENCHMARK(BM_MPSC_Pop);
BENCHMARK(BM_Mutex_Pop);

// ==================== 场景 3: 队列满时 push 失败率 ====================
template<typename Q>
void bench_push_full(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        Q q(64);
        for (int i = 0; i < 64; i++) q.push(i);  // 填满
        state.ResumeTiming();
        int failed = 0;
        for (int i = 0; i < 10000; i++) {
            if (!q.push(i)) failed++;
        }
        benchmark::DoNotOptimize(failed);
    }
}

static void BM_SPSC_PushFull(benchmark::State& state) { bench_push_full<spsc_queue<int>>(state); }
BENCHMARK(BM_SPSC_PushFull);

static void BM_Mutex_PushFull(benchmark::State& state) { bench_push_full<mutex_queue<int>>(state); }

BENCHMARK(BM_Mutex_PushFull);

// ==================== 场景 4: 1生产 1消费 ====================
template<typename Q>
void bench_prodcons(benchmark::State& state) {
    int total_msgs = state.range(0);
    int queue_size = state.range(1);
    for (auto _ : state) {
        state.PauseTiming();
        Q q(queue_size);
        std::atomic<int> produced{0}, consumed{0};
        state.ResumeTiming();

        std::thread consumer([&]() {
            int v;
            while (consumed < total_msgs) {
                if (q.pop(v)) consumed++;
            }
        });

        for (int i = 0; i < total_msgs; ++i) {
            while (!q.push(i)) {}
        }
        produced = total_msgs;
        consumer.join();

        benchmark::DoNotOptimize(produced);
        benchmark::DoNotOptimize(consumed);
    }
}

#define PRODCONS_ARGS ->Args({10000, 1024})->Args({100000, 65536})->Args({10000, 64})

static void BM_SPSC_ProdCons(benchmark::State& state) { bench_prodcons<spsc_queue<int>>(state); }
BENCHMARK(BM_SPSC_ProdCons) PRODCONS_ARGS;

static void BM_Mutex_ProdCons(benchmark::State& state) { bench_prodcons<mutex_queue<int>>(state); }

static void BM_MPSC_ProdCons(benchmark::State& state) { bench_prodcons<mpsc_queue<int>>(state); }
BENCHMARK(BM_MPSC_ProdCons) PRODCONS_ARGS;
BENCHMARK(BM_Mutex_ProdCons) PRODCONS_ARGS;

// ==================== 场景 5: 多生产者（2 个生产者 1 个消费者） ====================
template<typename Q>
void bench_multi_producer(benchmark::State& state) {
    int total_msgs = state.range(0);
    int num_producers = state.range(1);
    int queue_size = state.range(2);
    for (auto _ : state) {
        state.PauseTiming();
        Q q(queue_size);
        std::atomic<int> consumed{0};
        std::atomic<int> produced{0};
        state.ResumeTiming();

        std::thread consumer([&]() {
            int v;
            while (consumed < total_msgs) {
                if (q.pop(v)) consumed++;
            }
        });

        std::vector<std::thread> producers;
        int per_producer = total_msgs / num_producers;
        for (int p = 0; p < num_producers; ++p) {
            producers.emplace_back([&, p]() {
                for (int i = 0; i < per_producer; ++i) {
                    while (!q.push(p * per_producer + i)) {}
                    produced++;
                }
            });
        }
        for (auto& t : producers) t.join();
        consumer.join();

        benchmark::DoNotOptimize(produced);
        benchmark::DoNotOptimize(consumed);
    }
}

#define MULTI_ARGS ->Args({50000, 2, 1024})->Args({50000, 2, 65536})

static void BM_SPSC_MultiProd(benchmark::State& state) { bench_multi_producer<spsc_queue<int>>(state); }
BENCHMARK(BM_SPSC_MultiProd) MULTI_ARGS;

static void BM_Mutex_MultiProd(benchmark::State& state) { bench_multi_producer<mutex_queue<int>>(state); }
BENCHMARK(BM_Mutex_MultiProd) MULTI_ARGS;

// ==================== 场景 6: 固定负载，不同队列大小 ====================
template<typename Q>
void bench_queue_size_impact(benchmark::State& state) {
    int queue_size = state.range(0);
    for (auto _ : state) {
        state.PauseTiming();
        Q q(queue_size);
        int consumed = 0;
        state.ResumeTiming();

        std::thread consumer([&]() {
            int v;
            for (int i = 0; i < 50000; ++i) {
                while (!q.pop(v)) {}
                consumed++;
            }
        });

        for (int i = 0; i < 50000; ++i) {
            while (!q.push(i)) {}
        }
        consumer.join();
        benchmark::DoNotOptimize(consumed);
    }
}

#define SIZE_ARGS ->Arg(64)->Arg(1024)->Arg(65536)

static void BM_SPSC_SizeImpact(benchmark::State& state) { bench_queue_size_impact<spsc_queue<int>>(state); }
BENCHMARK(BM_SPSC_SizeImpact) SIZE_ARGS;

static void BM_Mutex_SizeImpact(benchmark::State& state) { bench_queue_size_impact<mutex_queue<int>>(state); }
BENCHMARK(BM_Mutex_SizeImpact) SIZE_ARGS;

BENCHMARK_MAIN();
