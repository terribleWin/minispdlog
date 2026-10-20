#include "framework/doctest.h"
#include "framework/mock_sink.h"
#include "minispdlog/minispdlog.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace minispdlog;
using minispdlog::tests::mock_sink_mt;

/** @file test_registry.cpp @brief 覆盖全局 logger 注册表与默认 logger 快照。 */

namespace {

class reentrant_destructor_logger final : public logger {
public:
    reentrant_destructor_logger(std::string name, std::atomic<bool>& entered, std::atomic<bool>& completed)
        : logger(std::move(name)), entered_(entered), completed_(completed) {}

    ~reentrant_destructor_logger() override {
        entered_.store(true, std::memory_order_release);
        static_cast<void>(registry::instance().get("registry-reentrant-probe"));
        completed_.store(true, std::memory_order_release);
    }

private:
    std::atomic<bool>& entered_;
    std::atomic<bool>& completed_;
};

} // namespace

TEST_CASE("registry singleton instance is consistent [registry]") {
    auto& reg1 = registry::instance();
    auto& reg2 = registry::instance();
    REQUIRE(&reg1 == &reg2); ///< 单例：同一地址
}

TEST_CASE("register and get logger [registry]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<logger>("test_reg", mock);

    register_logger(lg);

    auto retrieved = get("test_reg");
    REQUIRE(retrieved != nullptr);
    REQUIRE(retrieved->name() == "test_reg");

    drop("test_reg"); ///< 清理
}

TEST_CASE("register rejects null logger [registry][safety]") {
    REQUIRE_THROWS_AS(registry::instance().register_logger(nullptr), std::invalid_argument);
}

TEST_CASE("drop releases logger outside registry lock [registry][reentrant]") {
    drop_all();
    std::atomic<bool> entered{false};
    std::atomic<bool> completed{false};
    auto victim = std::make_shared<reentrant_destructor_logger>("registry-reentrant-drop", entered, completed);
    register_logger(victim);
    victim.reset();

    drop("registry-reentrant-drop");

    REQUIRE(entered.load(std::memory_order_acquire));
    REQUIRE(completed.load(std::memory_order_acquire));
}

TEST_CASE("drop_all releases loggers outside registry lock [registry][reentrant]") {
    drop_all();
    std::atomic<bool> entered{false};
    std::atomic<bool> completed{false};
    auto victim = std::make_shared<reentrant_destructor_logger>("registry-reentrant-all", entered, completed);
    register_logger(victim);
    victim.reset();

    drop_all();

    REQUIRE(entered.load(std::memory_order_acquire));
    REQUIRE(completed.load(std::memory_order_acquire));
}

TEST_CASE("get non-existent logger returns nullptr [registry]") {
    auto result = get("nonexistent_logger_xyz");
    REQUIRE(result == nullptr);
}

TEST_CASE("register duplicate logger throws [registry]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg1 = std::make_shared<logger>("dup_test", mock);
    auto lg2 = std::make_shared<logger>("dup_test", mock);

    register_logger(lg1);
    REQUIRE_THROWS_AS(register_logger(lg2), std::runtime_error);

    drop("dup_test"); ///< 清理
}

TEST_CASE("drop logger removes from registry [registry]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<logger>("drop_test", mock);
    register_logger(lg);

    REQUIRE(get("drop_test") != nullptr);
    drop("drop_test");
    REQUIRE(get("drop_test") == nullptr);
}

TEST_CASE("drop_all clears all loggers [registry]") {
    /// 注册多个 logger
    auto m1 = std::make_shared<mock_sink_mt>();
    auto m2 = std::make_shared<mock_sink_mt>();
    auto m3 = std::make_shared<mock_sink_mt>();
    register_logger(std::make_shared<logger>("a", m1));
    register_logger(std::make_shared<logger>("b", m2));
    register_logger(std::make_shared<logger>("c", m3));

    REQUIRE(get("a") != nullptr);
    REQUIRE(get("b") != nullptr);
    REQUIRE(get("c") != nullptr);

    drop_all();

    REQUIRE(get("a") == nullptr);
    REQUIRE(get("b") == nullptr);
    REQUIRE(get("c") == nullptr);
}

TEST_CASE("set_default_logger changes default [registry]") {
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<logger>("new_default", mock);

    set_default_logger(lg);

    auto def = default_logger();
    REQUIRE(def != nullptr);
    REQUIRE(def->name() == "new_default");

    /// 恢复默认（可选，避免影响其他测试）
    drop_all();
}

TEST_CASE("default logger exists even before explicit set [registry]") {
    /// 首次调用 default_logger() 如果不存在，registry 会创建一个默认的 stdout logger
    auto def = default_logger();
    REQUIRE(def != nullptr);
    /// 当前实现中默认 logger 名称为空字符串
    REQUIRE(def->name() == "");
}

TEST_CASE("global set_level applies to all registered loggers [registry][level]") {
    auto m1 = std::make_shared<mock_sink_mt>();
    auto m2 = std::make_shared<mock_sink_mt>();
    auto lg1 = std::make_shared<logger>("global1", m1);
    auto lg2 = std::make_shared<logger>("global2", m2);
    register_logger(lg1);
    register_logger(lg2);

    set_level(level::error);

    REQUIRE(lg1->should_log(level::info) == false);
    REQUIRE(lg1->should_log(level::error) == true);
    REQUIRE(lg2->should_log(level::info) == false);
    REQUIRE(lg2->should_log(level::error) == true);

    drop_all();
}

TEST_CASE("registry thread safety [registry][thread]") {
    /// 多线程并发注册/获取/删除，不应崩溃
    const int num_threads = 8;
    const int ops_per_thread = 50;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, ops_per_thread]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                std::string name = "thread_" + std::to_string(i) + "_" + std::to_string(j);
                auto mock = std::make_shared<mock_sink_mt>();
                auto lg = std::make_shared<logger>(name, mock);
                try {
                    register_logger(lg);
                } catch (...) {
                    /// 重复注册可能抛异常，忽略
                }
                /// 获取
                auto got = get(name);
                /// 删除
                drop(name);
            }
        });
    }
    for (auto& t : threads)
        t.join();

    /// 全部清理
    drop_all();
    REQUIRE(true); ///< 只要没崩溃就算通过（TSan 会检测 race）
}

TEST_CASE("flush_all does not throw even with file sinks [registry][flush]") {
    auto m1 = std::make_shared<mock_sink_mt>();
    auto m2 = std::make_shared<mock_sink_mt>();
    register_logger(std::make_shared<logger>("f1", m1));
    register_logger(std::make_shared<logger>("f2", m2));

    REQUIRE_NOTHROW(flush_all());

    drop_all();
}

TEST_CASE("drop_all then set_level/flush_all is safe [registry][safety]") {
    /// 先注册一个 logger
    auto mock = std::make_shared<mock_sink_mt>();
    auto lg = std::make_shared<logger>("survivor", mock);
    register_logger(lg);

    /// 清空所有
    drop_all();

    /// 此时 default_logger 可能为 nullptr，但 set_level/flush_all 不应崩溃
    REQUIRE_NOTHROW(set_level(level::warn));
    REQUIRE_NOTHROW(flush_all());
}

TEST_CASE("default logger supports concurrent read replace and drop [registry][thread]") {
    drop_all();
    auto sink = std::make_shared<mock_sink_mt>();
    auto first = std::make_shared<logger>("default-a", sink);
    auto second = std::make_shared<logger>("default-b", sink);
    auto lookup = std::make_shared<logger>("lookup", sink);
    register_logger(lookup);
    set_default_logger(first);

    constexpr int iterations = 2000;
    std::atomic<bool> start{false};
    std::atomic<bool> invalid_snapshot{false};
    std::vector<std::thread> threads;

    threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 0; i < iterations; ++i) {
            set_default_logger((i & 1) == 0 ? first : second);
        }
    });

    for (int reader = 0; reader < 3; ++reader) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < iterations; ++i) {
                auto current = registry::instance().default_logger();
                if (current && current != first && current != second) {
                    invalid_snapshot.store(true, std::memory_order_relaxed);
                }
                auto found = get("lookup");
                if (found && found != lookup) {
                    invalid_snapshot.store(true, std::memory_order_relaxed);
                }
            }
        });
    }

    threads.emplace_back([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 0; i < iterations; ++i) {
            drop("default-a");
            drop("missing-default");
        }
    });

    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }

    set_default_logger(second);
    REQUIRE_FALSE(invalid_snapshot.load(std::memory_order_relaxed));
    REQUIRE(registry::instance().default_logger() == second);
    REQUIRE(get("lookup") == lookup);
    drop_all();
    REQUIRE(registry::instance().default_logger() == nullptr);
}

TEST_CASE("drop of retired name keeps replacement default logger [registry][safety]") {
    drop_all();
    auto sink = std::make_shared<mock_sink_mt>();
    auto retired = std::make_shared<logger>("retired", sink);
    auto replacement = std::make_shared<logger>("replacement", sink);
    register_logger(retired);
    set_default_logger(retired);

    set_default_logger(replacement);
    drop("retired");

    REQUIRE(get("retired") == nullptr);
    REQUIRE(registry::instance().default_logger() == replacement);
    drop_all();
}

TEST_CASE("factory functions register logger automatically [registry][factory]") {
    auto lg = stdout_color_mt("factory_test");
    REQUIRE(lg != nullptr);
    REQUIRE(get("factory_test") != nullptr);
    REQUIRE(get("factory_test") == lg); ///< 工厂函数创建的 logger 已经被注册
    drop("factory_test");
}
