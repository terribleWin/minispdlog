#include "framework/doctest.h"
#include "minispdlog/minispdlog.h"
#include "minispdlog/lockfree_queue.h"
#include "minispdlog/details/circular_q.h"
#include "minispdlog/details/mpmc_blocking_q.h"
#include "minispdlog/details/mpsc_queue.h"
#include "minispdlog/details/spsc_queue.h"
#include "minispdlog/details/queue_utils.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <set>
#include <vector>

using namespace minispdlog;
using namespace minispdlog::details;

// ============================================================
// 测试套件：队列系统 (circular / mpmc / spsc / mpsc lockfree)
// 标签: [queue]
// ============================================================

TEST_CASE("next_power_of_two helper [queue][utils]") {
    REQUIRE(next_power_of_two(0) == 2);
    REQUIRE(next_power_of_two(1) == 2);
    REQUIRE(next_power_of_two(2) == 2);
    REQUIRE(next_power_of_two(3) == 4);
    REQUIRE(next_power_of_two(5) == 8);
    REQUIRE(next_power_of_two(8) == 8);
}

// ---------- circular_q ----------

TEST_CASE("circular_q basic operations [queue][circular]") {
    circular_q<int> q(5);
    REQUIRE(q.capacity() == 5);
    REQUIRE(q.size() == 0);
    REQUIRE(q.empty() == true);
    REQUIRE(q.full() == false);

    q.push_back(1);
    q.push_back(2);
    q.push_back(3);
    REQUIRE(q.size() == 3);
    REQUIRE(q.front() == 1);

    q.pop_front();
    REQUIRE(q.size() == 2);
    REQUIRE(q.front() == 2);
}

TEST_CASE("circular_q fills to capacity [queue][circular]") {
    circular_q<int> q(3);
    q.push_back(1);
    q.push_back(2);
    q.push_back(3);
    REQUIRE(q.full() == true);
    REQUIRE(q.size() == 3);
}

TEST_CASE("circular_q overwrites oldest when full [queue][circular]") {
    circular_q<int> q(3);
    q.push_back(1);
    q.push_back(2);
    q.push_back(3);
    REQUIRE(q.front() == 1);

    q.push_back(4);  // 覆盖最旧的 (1)
    REQUIRE(q.front() == 2);
    REQUIRE(q.overrun_count() == 1);
}

TEST_CASE("circular_q multiple overwrites [queue][circular]") {
    circular_q<int> q(2);
    q.push_back(1);
    q.push_back(2);
    q.push_back(3);  // 覆盖 1
    q.push_back(4);  // 覆盖 2
    q.push_back(5);  // 覆盖 3

    REQUIRE(q.front() == 4);
    REQUIRE(q.size() == 2);
    REQUIRE(q.overrun_count() == 3);
}

// ---------- mpmc_blocking_queue ----------

TEST_CASE("mpmc_blocking_queue basic enqueue dequeue [queue][mpmc]") {
    mpmc_blocking_queue<int> q(5);
    q.enqueue(10);
    q.enqueue(20);

    int val = 0;
    REQUIRE(q.dequeue_for(val, std::chrono::milliseconds(100)) == true);
    REQUIRE(val == 10);
    REQUIRE(q.dequeue_for(val, std::chrono::milliseconds(100)) == true);
    REQUIRE(val == 20);
}

TEST_CASE("mpmc_blocking_queue dequeue timeout on empty [queue][mpmc]") {
    mpmc_blocking_queue<int> q(5);
    int val = 0;
    auto start = std::chrono::steady_clock::now();
    bool success = q.dequeue_for(val, std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(success == false);
    REQUIRE(elapsed >= std::chrono::milliseconds(50));
}

TEST_CASE("mpmc_blocking_queue enqueue_nowait does not block [queue][mpmc]") {
    mpmc_blocking_queue<int> q(2);
    q.enqueue_nowait(1);
    q.enqueue_nowait(2);
    // 队列已满，但 nowait 不会阻塞，直接覆盖最旧
    q.enqueue_nowait(3);

    REQUIRE(q.size() == 2);  // 容量�?2
}

TEST_CASE("mpmc_blocking_queue concurrent producer consumer [queue][mpmc][thread]") {
    mpmc_blocking_queue<int> q(100);
    const int num_items = 1000;

    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            q.enqueue(std::move(i));
        }
    });

    std::atomic<int> consumed{0};
    std::thread consumer([&]() {
        int val;
        while (consumed < num_items) {
            if (q.dequeue_for(val, std::chrono::milliseconds(100))) {
                ++consumed;
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumed == num_items);
}

TEST_CASE("mpmc_blocking_queue multiple producers multiple consumers [queue][mpmc][thread]") {
    mpmc_blocking_queue<int> q(200);
    const int producers = 4;
    const int consumers = 2;
    const int items_per_producer = 500;

    std::atomic<int> total_consumed{0};
    std::vector<std::thread> threads;

    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                q.enqueue(std::move(p * items_per_producer + i));
            }
        });
    }

    for (int c = 0; c < consumers; ++c) {
        threads.emplace_back([&]() {
            int val;
            while (total_consumed < producers * items_per_producer) {
                if (q.dequeue_for(val, std::chrono::milliseconds(100))) {
                    ++total_consumed;
                }
            }
        });
    }

    for (auto& t : threads) t.join();
    REQUIRE(total_consumed == producers * items_per_producer);
}

// ---------- mpsc_queue ----------

TEST_CASE("mpsc_queue basic push pop [queue][mpsc]") {
    mpsc_queue<int> q(5);
    REQUIRE(q.empty() == true);

    REQUIRE(q.push(1) == true);
    REQUIRE(q.push(2) == true);
    REQUIRE(q.size() == 2);

    int val = 0;
    REQUIRE(q.pop(val) == true);
    REQUIRE(val == 1);
    REQUIRE(q.pop(val) == true);
    REQUIRE(val == 2);
    REQUIRE(q.empty() == true);
}

TEST_CASE("mpsc_queue push fails when full [queue][mpsc]") {
    mpsc_queue<int> q(2);
    REQUIRE(q.push(1) == true);
    REQUIRE(q.push(2) == true);
    REQUIRE(q.push(3) == false);  // 已满，应失败
}

TEST_CASE("mpsc_queue concurrent producers single consumer [queue][mpsc][thread]") {
    mpsc_queue<int> q(1000);
    const int producers = 4;
    const int items_per_producer = 250;
    const int total = producers * items_per_producer;

    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                while (!q.push(p * items_per_producer + i)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector<int> got;
    got.reserve(static_cast<std::size_t>(total));
    std::thread consumer([&]() {
        int val;
        while (static_cast<int>(got.size()) < total) {
            if (q.pop(val)) {
                got.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& t : threads) t.join();
    consumer.join();

    REQUIRE(static_cast<int>(got.size()) == total);
    std::set<int> unique(got.begin(), got.end());
    REQUIRE(static_cast<int>(unique.size()) == total);
}

TEST_CASE("mpsc_queue push failure leaves value intact [queue][mpsc]") {
    mpsc_queue<int> q(2);
    REQUIRE(q.capacity() == 2);
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    int third = 42;
    REQUIRE(q.push(std::move(third)) == false);
    REQUIRE(third == 42);
}

TEST_CASE("mpsc_queue concurrent full-queue retries do not drop tickets [queue][mpsc][thread]") {
    mpsc_queue<int> q(8);
    const int producers = 8;
    const int items_per_producer = 200;
    const int total = producers * items_per_producer;
    std::atomic<int> accepted{0};

    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; ++i) {
                const int value = p * items_per_producer + i;
                while (!q.push(value)) {
                    std::this_thread::yield();
                }
                accepted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<int> got;
    got.reserve(static_cast<std::size_t>(total));
    std::thread consumer([&]() {
        int val = 0;
        while (static_cast<int>(got.size()) < total) {
            if (q.pop(val)) {
                got.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& t : threads) {
        t.join();
    }
    consumer.join();

    REQUIRE(accepted.load() == total);
    REQUIRE(static_cast<int>(got.size()) == total);
    std::set<int> unique(got.begin(), got.end());
    REQUIRE(static_cast<int>(unique.size()) == total);
}

// ---------- spsc_queue ----------

TEST_CASE("spsc_queue basic push pop [queue][spsc]") {
    spsc_queue<int> q(4);
    REQUIRE(q.capacity() == 4);
    REQUIRE(q.empty());
    REQUIRE(q.push(10));
    REQUIRE(q.push(20));
    REQUIRE(q.size() == 2);

    int v = 0;
    REQUIRE(q.pop(v));
    REQUIRE(v == 10);
    REQUIRE(q.pop(v));
    REQUIRE(v == 20);
    REQUIRE(q.empty());
}

TEST_CASE("spsc_queue rounds capacity to power of two [queue][spsc]") {
    spsc_queue<int> q(5);
    REQUIRE(q.capacity() == 8);
}

TEST_CASE("spsc_queue push fails when full [queue][spsc]") {
    spsc_queue<int> q(2);
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE(q.push(3) == false);
}

TEST_CASE("spsc_queue concurrent producer consumer [queue][spsc][thread]") {
    spsc_queue<int> q(1024);
    const int n = 5000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::thread producer([&]() {
        for (int i = 0; i < n; ++i) {
            while (!q.push(i)) {
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&]() {
        int val;
        while (consumed.load(std::memory_order_relaxed) < n) {
            if (q.pop(val)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    REQUIRE(produced == n);
    REQUIRE(consumed == n);
}

TEST_CASE("mpmc try_enqueue discards when full [queue][mpmc]") {
    mpmc_blocking_queue<int> q(2);
    REQUIRE(q.try_enqueue(1));
    REQUIRE(q.try_enqueue(2));
    REQUIRE(q.try_enqueue(3) == false);

    int v = 0;
    REQUIRE(q.dequeue_for(v, std::chrono::milliseconds(50)));
    REQUIRE(v == 1);
}
