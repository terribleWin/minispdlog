#pragma once
#include "common.h"
#include "logger.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace minispdlog {

/**
 * @brief 进程级 logger 注册表与默认 logger 管理器。
 *
 * 注册表只在查找或变更映射时持锁；`flush_all()` 会先复制快照，再在锁外执行
 * 可能阻塞的 Sink I/O，避免用户回调重入造成死锁。
 *
 * @par 线程安全性
 * 所有公开成员函数均可并发调用。
 */
class MINISPDLOG_API registry {
public:
    registry(const registry&) = delete;
    registry& operator=(const registry&) = delete;

    /** @brief 返回进程内唯一注册表实例。 */
    static registry& instance();

    /**
     * @brief 按 logger 自带名称注册实例。
     * @throws std::runtime_error 名称已存在时抛出。
     */
    void register_logger(std::shared_ptr<logger> new_logger);

    /** @brief 查找名称对应的 logger；不存在时返回空指针。 */
    std::shared_ptr<logger> get(const std::string& logger_name);

    /** @brief 移除指定 logger；名称不存在时不执行操作。 */
    void drop(const std::string& logger_name);

    /** @brief 移除全部已注册 logger 与默认 logger。 */
    void drop_all();

    /** @brief 返回默认 logger 的共享所有权；未设置时返回空指针。 */
    std::shared_ptr<logger> default_logger();

    /** @brief 替换默认 logger；传入空指针可清除。 */
    void set_default_logger(std::shared_ptr<logger> new_default_logger);

    /**
     * @brief 仅在默认 logger 为空时发布候选实例。
     * @param candidate 候选 logger。
     * @return 成功发布时返回 candidate；已有默认 logger 时返回现有实例。
     * @note 用于 `default_logger()` 的并发惰性初始化，避免不同线程返回不同实例。
     */
    std::shared_ptr<logger> set_default_logger_if_empty(std::shared_ptr<logger> candidate);

    /** @brief 设置默认 logger 与全部已注册 logger 的过滤级别。 */
    void set_level(level log_level);

    /** @brief 刷新默认 logger 与全部已注册 logger。 */
    void flush_all();

private:
    registry();
    ~registry() = default;
    void throw_if_exists(const std::string& logger_name);

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<logger>> loggers_;
    details::atomic_shared_ptr<logger> default_logger_;
};

} ///< 结束 minispdlog 命名空间。
