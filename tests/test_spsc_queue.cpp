#include "minispdlog/details/spsc_queue.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace minispdlog::details;

// 测试 1：基础入队出队
void test_basic() {
    std::cout << "测试1: 基础入队出队...\n";
    spsc_queue<int> q(4);

    assert(q.empty());
    assert(q.push(1));
    assert(q.push(2));
    assert(!q.empty());
    assert(q.size() == 2);

    int val;
    assert(q.pop(val) && val == 1);
    assert(q.pop(val) && val == 2);
    assert(q.empty());
    std::cout << "  PASS\n";
}

// 测试 2：队列满时返回 false
void test_full() {
    std::cout << "测试2: 队列满时返回 false...\n";
    spsc_queue<int> q(2);

    assert(q.push(1));
    assert(q.push(2));
    assert(!q.push(3)); // 容量 2，已满

    int val;
    assert(q.pop(val) && val == 1);
    assert(q.push(3)); // 腾出空位后可以再入
    std::cout << "  PASS\n";
}

// 测试 3：生产者-消费者 1对1
void test_spsc_concurrent() {
    std::cout << "测试3: 单生产者单消费者并发...\n";
    spsc_queue<int> q(1024);
    const int N = 50000;
    std::atomic<int> produced{0}, consumed{0};

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) {
            while (!q.push(i)) {
                std::this_thread::yield();
            }
            produced++;
        }
    });

    std::thread consumer([&]() {
        int val;
        while (consumed < N) {
            if (q.pop(val)) {
                consumed++;
            }
        }
    });

    producer.join();
    consumer.join();

    assert(produced == N);
    assert(consumed == N);
    std::cout << "  PASS (" << N << " 条消息无损)\n";
}

// 测试 4：性能对比（粗略）
void test_perf() {
    std::cout << "测试4: 入队性能...\n";
    spsc_queue<int> q(65536);
    const int N = 50000;

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        while (!q.push(i)) {
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "  " << N << " 次 push, 平均 " << (ns / N) << " ns/op\n";
}

int main() {
    std::cout << "=== SPSC 无锁队列测试 ===\n\n";
    test_basic();
    test_full();
    test_spsc_concurrent();
    test_perf();
    std::cout << "\n所有测试通过!\n";
    return 0;
}
