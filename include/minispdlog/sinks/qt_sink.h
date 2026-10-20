#pragma once

/**
 * @file qt_sink.h
 * @brief 将日志安全投递到 Qt GUI 事件循环的 Sink。
 *
 * 依赖 Qt5/Qt6 Widgets，仅在定义 `MINISPDLOG_WITH_QT` 且链接 Qt::Widgets 时使用。
 *
 * 日志可来自业务线程或异步工作线程，Sink 使用 `QMetaObject::invokeMethod()` 与
 * `Qt::AutoConnection` 将调用投递到目标对象所属线程。Sink 不拥有 `QObject`；
 * 控件销毁前，应用必须先移除 Sink 或停止 logger。
 */

#ifndef MINISPDLOG_WITH_QT
#error "qt_sink.h requires MINISPDLOG_WITH_QT. Enable -DMINISPDLOG_WITH_QT=ON and link Qt Widgets."
#endif

#include "../logger.h"
#include "../registry.h"
#include "base_sink.h"

#include <QMetaObject>
#include <QObject>
#include <QString>

#include <memory>
#include <stdexcept>
#include <string>

namespace minispdlog {
namespace sinks {

    /**
 * @brief 调用 QObject 元方法显示日志的 Qt Sink。
 * @tparam Mutex Sink 使用的互斥类型。
 */
    template <typename Mutex>
    class qt_sink : public base_sink<Mutex> {
    public:
        /**
     * @brief 创建 Qt 输出 Sink。
     * @param qt_object 目标对象，如 `QTextEdit*` 或 `QPlainTextEdit*`。
     * @param meta_method 可调用方法名；`QTextEdit` 默认使用 `append`，
     * `QPlainTextEdit` 应传入 `appendPlainText`。
     * @throws std::invalid_argument 对象为空或方法名为空时抛出。
     */
        qt_sink(QObject* qt_object, std::string meta_method = "append")
            : qt_object_(qt_object), meta_method_(std::move(meta_method)) {
            if (qt_object_ == nullptr) {
                throw std::invalid_argument("qt_sink: qt_object must not be null");
            }
            if (meta_method_.empty()) {
                throw std::invalid_argument("qt_sink: meta_method must not be empty");
            }
        }

    protected:
        void sink_it_(const details::log_msg& msg) override {
            auto& formatted = this->format_message(msg);

            /// `append` 类方法会自行换行，因此投递前移除格式化器的行结束符。
            std::string line(formatted.data(), formatted.size());
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }

            const QString qline = QString::fromUtf8(line.data(), static_cast<int>(line.size()));

            /// 同线程直接调用，跨线程则排队到 GUI 事件循环。
            QMetaObject::invokeMethod(qt_object_, meta_method_.c_str(), Qt::AutoConnection, Q_ARG(QString, qline));
        }

        void flush_() override {
            /// GUI 控件没有显式刷新接口，由事件循环完成投递。
        }

    private:
        QObject* qt_object_;
        std::string meta_method_;
    };

    using qt_sink_mt = qt_sink<std::mutex>;
    using qt_sink_st = qt_sink<null_mutex>;

} ///< 结束 sinks 命名空间。

/**
 * @brief 创建多线程安全的 Qt logger 并注册到全局注册表。
 */
inline std::shared_ptr<logger> qt_logger_mt(const std::string& logger_name, QObject* qt_object,
                                            const std::string& meta_method = "append") {
    auto sink = std::make_shared<sinks::qt_sink_mt>(qt_object, meta_method);
    auto lg = std::make_shared<logger>(logger_name, sink);
    registry::instance().register_logger(lg);
    return lg;
}

inline std::shared_ptr<logger> qt_logger_st(const std::string& logger_name, QObject* qt_object,
                                            const std::string& meta_method = "append") {
    auto sink = std::make_shared<sinks::qt_sink_st>(qt_object, meta_method);
    auto lg = std::make_shared<logger>(logger_name, sink);
    registry::instance().register_logger(lg);
    return lg;
}

} ///< 结束 minispdlog 命名空间。
