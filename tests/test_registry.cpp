#include "minispdlog/minispdlog.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace minispdlog;

void test_basic_registry() {
    std::cout << "\n========== 测试1:基础注册和获取 ==========\n";
    
    // 创建并注册 logger
    auto console_sink = std::make_shared<sinks::console_sink_mt>();
    auto my_logger = std::make_shared<logger>("my_logger", console_sink);
    register_logger(my_logger);
    
    // 获取 logger
    auto retrieved = get("my_logger");
    if (retrieved) {
        std::cout << " 成功获取 logger: " << retrieved->name() << "\n";
        retrieved->info("Hello from retrieved logger!");
    } else {
        std::cout << " 未找到 logger\n";
    }
    
    // 尝试获取不存在的 logger
    auto not_exist = get("non_existent");
    if (!not_exist) {
        std::cout << " 不存在的 logger 返回 nullptr\n";
    }
    
    drop("my_logger");
}

void test_duplicate_registration() {
    std::cout << "\n========== 测试2:重复注册异常处理 ==========\n";
    
    auto sink = std::make_shared<sinks::console_sink_mt>();
    auto logger1 = std::make_shared<logger>("duplicate_test", sink);
    
    try {
        register_logger(logger1);
        std::cout << " 第一次注册成功\n";
        
        // 尝试重复注册
        auto logger2 = std::make_shared<logger>("duplicate_test", sink);
        register_logger(logger2);
        std::cout << " 重复注册没有抛出异常(错误!)\n";
    } catch (const std::runtime_error& e) {
        std::cout << " 捕获异常: " << e.what() << "\n";
    }
    
    drop("duplicate_test");
}

void test_default_logger() {
    std::cout << "\n========== 测试3:默认 Logger ==========\n";
    
    // 使用默认 logger
    auto def_log = default_logger();
    std::cout << "默认 logger 名称: [" << def_log->name() << "]\n";
    
    def_log->info("This is from default logger");
    def_log->warn("Warning from default logger");
    
    // 替换默认 logger
    auto new_default = stdout_color_mt("new_default");
    set_default_logger(new_default);
    
    auto updated_def = default_logger();
    std::cout << "新的默认 logger 名称: [" << updated_def->name() << "]\n";
    updated_def->info("This is from new default logger");
    
    drop("new_default");
}

void test_global_logging() {
    std::cout << "\n========== 测试4:全局日志接口 ==========\n";

    // 直接使用全局函数(操作默认 logger)
    info("Global info message");
    warn("Global warning");
    error("Global error");
    
    std::cout << "\n修改默认 logger 的格式:\n";
    
    auto def = default_logger();
    auto custom_sink = std::make_shared<sinks::color_console_sink_mt>();
    custom_sink->set_formatter(
        std::make_unique<pattern_formatter>("[全局] [%H:%M:%S] [%l] %v")
    );
    
    auto custom_logger = std::make_shared<logger>("global_custom", custom_sink);
    set_default_logger(custom_logger);
    
    info("Customized global info");
    warn("Customized global warning");
    
    drop("global_custom");
}

void test_factory_functions() {
    std::cout << "\n========== 测试5:工厂函数 ==========\n";
    
    // 使用工厂函数创建各种 logger
    auto console_log = stdout_color_mt("console_logger");
    auto stderr_log = stderr_color_mt("stderr_logger");
    auto file_log = basic_logger_mt("file_logger", "logs/factory_test.log", true);
    
    console_log->info("Message from console logger");
    stderr_log->error("Error from stderr logger");
    file_log->info("Message from file logger");
    
    // 从 registry 获取
    auto retrieved_console = get("console_logger");
    if (retrieved_console) {
        retrieved_console->debug("Retrieved from registry");
    }
    
    std::cout << " 工厂函数测试完成\n";
    
    drop("console_logger");
    drop("stderr_logger");
    drop("file_logger");
}

void test_multiple_loggers() {
    std::cout << "\n========== 测试6:多 Logger 场景 ==========\n";
    
    // 创建多个不同用途的 logger
    auto app_logger = stdout_color_mt("app");
    auto db_logger = basic_logger_mt("database", "logs/database.log", true);
    auto net_logger = stdout_color_mt("network");
    
    // 不同组件使用不同 logger
    get("app")->info("Application started");
    get("database")->info("Connecting to database...");
    get("database")->info("Connection established");
    get("network")->debug("Listening on port 8080");
    get("app")->warn("Low memory warning");
    
    std::cout << "\n 场景模拟:不同模块使用独立的 logger\n";
    
    drop("app");
    drop("database");
    drop("network");
}

void test_global_level_control() {
    std::cout << "\n========== 测试7:全局级别控制 ==========\n";
    
    auto log1 = stdout_color_mt("logger1");
    auto log2 = stdout_color_mt("logger2");
    auto log3 = stdout_color_mt("logger3");
    
    std::cout << "设置前:\n";
    log1->trace("Trace from logger1");
    log2->debug("Debug from logger2");
    log3->info("Info from logger3");
    
    std::cout << "\n设置全局级别为 debug:\n";
    set_level(level::debug);
    
    log1->trace("Trace from logger1 (filtered)");
    log1->debug("Debug from logger1");
    log2->debug("Debug from logger2");
    log3->info("Info from logger3");
    
    drop("logger1");
    drop("logger2");
    drop("logger3");
}

void test_drop_all() {
    std::cout << "\n========== 测试8:批量删除 ==========\n";
    
    stdout_color_mt("temp1");
    stdout_color_mt("temp2");
    stdout_color_mt("temp3");
    
    std::cout << "创建了 3 个临时 logger\n";
    
    drop_all();
    
    if (!get("temp1") && !get("temp2") && !get("temp3")) {
        std::cout << " drop_all() 成功清空所有 logger\n";
    }
}

void test_thread_safety() {
    std::cout << "\n========== 测试9:线程安全 ==========\n";
    
    auto shared_logger = stdout_color_mt("shared");
    
    auto thread_func = [](int id) {
        for (int i = 0; i < 3; ++i) {
            auto log = get("shared");
            if (log) {
                log->info("Thread {} - Message {}", id, i);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };
    
    std::thread t1(thread_func, 1);
    std::thread t2(thread_func, 2);
    std::thread t3(thread_func, 3);
    
    t1.join();
    t2.join();
    t3.join();
    
    std::cout << " 多线程访问 registry 完成\n";
    
    drop("shared");
}

void test_real_world_scenario() {
    std::cout << "\n========== 测试10:真实场景模拟 ==========\n";
    
    // 模拟应用程序的日志配置
    
    // 1. 主应用日志:彩色控制台
    auto app = stdout_color_mt("app");
    app->set_level(level::debug);
    
    // 2. 数据库日志:文件
    auto db = basic_logger_mt("database", "logs/db.log", false);
    db->set_level(level::trace);
    
    // 3. 网络日志:文件
    auto net = basic_logger_mt("network", "logs/network.log", false);
    net->set_level(level::info);
    
    // 4. 错误日志:单独的文件
    auto err = basic_logger_mt("errors", "logs/errors.log", false);
    err->set_level(level::error);
    
    // 5. 设置默认 logger 为 app
    set_default_logger(app);
    
    // 模拟应用运行
    std::cout << "\n--- 应用启动 ---\n";
    info("Application starting...");
    
    get("database")->debug("Initializing database connection pool");
    get("database")->info("Database connected");
    
    get("network")->info("Starting HTTP server on port 8080");
    
    info("All services started successfully");
    
    std::cout << "\n--- 处理请求 ---\n";
    get("network")->debug("Received HTTP GET /api/users");
    get("database")->trace("SELECT * FROM users");
    get("database")->debug("Query returned 150 rows");
    
    warn("Memory usage: 85%");
    
    std::cout << "\n--- 错误处理 ---\n";
    get("database")->error("Connection lost to database");
    get("errors")->error("Database connection lost at 2025-10-10 14:30:00");
    
    error("Critical: Database unavailable");
    
    std::cout << "\n 查看日志文件:\n";
    std::cout << "  - logs/db.log (包含 trace/debug)\n";
    std::cout << "  - logs/network.log (info 及以上)\n";
    std::cout << "  - logs/errors.log (仅 error)\n";
    
    // 清理
    drop_all();
}

void test_logger_lifetime() {
    std::cout << "\n========== 测试11:Logger 生命周期 ==========\n";
    
    {
        auto temp_logger = stdout_color_mt("temp_scoped");
        temp_logger->info("Logger created in scope");
        
        // 在作用域内,logger 被 registry 持有
        auto retrieved = get("temp_scoped");
        std::cout << " Logger 在 registry 中: " << (retrieved ? "是" : "否") << "\n";
    }
    
    // 出作用域后,原始 shared_ptr 销毁,但 registry 仍持有
    auto still_exists = get("temp_scoped");
    if (still_exists) {
        std::cout << " Registry 保持 logger 存活\n";
        still_exists->info("Still alive in registry");
    }
    
    // 从 registry 中移除
    drop("temp_scoped");
    
    auto after_drop = get("temp_scoped");
    if (!after_drop) {
        std::cout << " drop() 后 logger 被释放\n";
    }
}

void test_custom_default_pattern() {
    std::cout << "\n========== 测试12:自定义默认 Logger 格式 ==========\n";
    
    // 创建自定义格式的默认 logger
    auto custom_sink = std::make_shared<sinks::color_console_sink_mt>();
    custom_sink->set_formatter(
        std::make_unique<pattern_formatter>("【%Y-%m-%d %H:%M:%S】【%L】%v")
    );
    
    auto custom_default = std::make_shared<logger>("custom_default", custom_sink);
    set_default_logger(custom_default);
    
    // 使用全局接口
    info("自定义格式的日志");
    warn("警告消息");
    error("错误消息");
    
    drop("custom_default");
}

void test_flush_all() {
    std::cout << "\n========== 测试13:全局刷新 ==========\n";
    
    auto file1 = basic_logger_mt("file1", "logs/flush1.log", true);
    auto file2 = basic_logger_mt("file2", "logs/flush2.log", true);
    
    file1->info("Message 1 to file1");
    file2->info("Message 1 to file2");
    file1->info("Message 2 to file1");
    file2->info("Message 2 to file2");
    
    std::cout << "写入多条消息...\n";
    flush_all();
    std::cout << " 执行 flush_all(),所有 logger 已刷新到磁盘\n";
    
    drop_all();
}

int main() {
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║ MiniSpdlog 第5天测试 - Registry系统     ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";
    
    // 创建日志目录
    system("mkdir -p logs");
    
    try {
        test_basic_registry();
        test_duplicate_registration();
        test_default_logger();
        test_global_logging();
        test_factory_functions();
        test_multiple_loggers();
        test_global_level_control();
        test_drop_all();
        test_thread_safety();
        test_real_world_scenario();
        test_logger_lifetime();
        test_custom_default_pattern();
        test_flush_all();
        
        std::cout << "\n 所有测试通过!\n\n";
    } catch (const std::exception& e) {
        std::cerr << "\n 测试失败: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}