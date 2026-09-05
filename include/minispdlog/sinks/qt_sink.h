#pragma once

/**
 * @file qt_sink.h
 * @brief Qt GUI 输出 Sink（参考 spdlog::qt_sink）
 *
 * 依赖：Qt5/Qt6 Widgets。仅在定义 MINISPDLOG_WITH_QT 且链接 Qt::Widgets 时使用。
 *
 * 线程模型：
 *   - 日志可能来自任意业务线程或 async 工作线程
 *   - 通过 QMetaObject::invokeMethod(..., Qt::AutoConnection) 投递到 GUI 线程
 *   - 目标对象须提供可被元对象系统调用的方法（如 QTextEdit::append）
 *
 * 生命周期：
 *   - 本 Sink 不拥有 QObject；若控件先于 logger 销毁，调用方需自行保证不再写日志
 *   - 推荐用 QPointer 由应用层在窗口关闭时 drop logger / 换 sink
 */

#ifndef MINISPDLOG_WITH_QT
#error "qt_sink.h requires MINISPDLOG_WITH_QT. Enable -DMINISPDLOG_WITH_QT=ON and link Qt Widgets."
#endif

#include "base_sink.h"
#include "../logger.h"
#include "../registry.h"

#include <QMetaObject>
#include <QObject>
#include <QString>

#include <memory>
#include <stdexcept>
#include <string>

namespace minispdlog {
namespace sinks {

template<typename Mutex>
class qt_sink : public base_sink<Mutex> {
public:
    /**
     * @param qt_object   目标 QObject（如 QTextEdit* / QPlainTextEdit*）
     * @param meta_method 槽/可调用方法名，默认 "append"（QTextEdit）；
     *                    QPlainTextEdit 请用 "appendPlainText"
     */
    qt_sink(QObject* qt_object, std::string meta_method = "append")
        : qt_object_(qt_object)
        , meta_method_(std::move(meta_method)) {
        if (qt_object_ == nullptr) {
            throw std::invalid_argument("qt_sink: qt_object must not be null");
        }
        if (meta_method_.empty()) {
            throw std::invalid_argument("qt_sink: meta_method must not be empty");
        }
    }

protected:
    void sink_it_(const details::log_msg& msg) override {
        fmt::memory_buffer formatted;
        this->format_message(msg, formatted);

        // 去掉末尾换行：QTextEdit::append 会自行换行
        std::string line(formatted.data(), formatted.size());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        const QString qline = QString::fromUtf8(line.data(), static_cast<int>(line.size()));

        // AutoConnection：同线程直接调，跨线程排队到 GUI 事件循环
        QMetaObject::invokeMethod(
            qt_object_,
            meta_method_.c_str(),
            Qt::AutoConnection,
            Q_ARG(QString, qline));
    }

    void flush_() override {
        // GUI 控件无显式 flush；事件循环处理即可
    }

private:
    QObject* qt_object_;
    std::string meta_method_;
};

using qt_sink_mt = qt_sink<std::mutex>;
using qt_sink_st = qt_sink<null_mutex>;

} // namespace sinks

/**
 * @brief 创建写入 QObject 的 logger 并注册到 registry
 */
inline std::shared_ptr<logger> qt_logger_mt(
    const std::string& logger_name,
    QObject* qt_object,
    const std::string& meta_method = "append") {
    auto sink = std::make_shared<sinks::qt_sink_mt>(qt_object, meta_method);
    auto lg = std::make_shared<logger>(logger_name, sink);
    registry::instance().register_logger(lg);
    return lg;
}

inline std::shared_ptr<logger> qt_logger_st(
    const std::string& logger_name,
    QObject* qt_object,
    const std::string& meta_method = "append") {
    auto sink = std::make_shared<sinks::qt_sink_st>(qt_object, meta_method);
    auto lg = std::make_shared<logger>(logger_name, sink);
    registry::instance().register_logger(lg);
    return lg;
}

} // namespace minispdlog
