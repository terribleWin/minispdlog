#include "framework/doctest.h"

#ifndef MINISPDLOG_WITH_QT
TEST_CASE("qt_sink skipped without MINISPDLOG_WITH_QT [sink][qt][skip]") {
    // 未启用 Qt 时本文件仍可编译进套件，但用例标记为跳过语义（空通过）
    CHECK(true);
}
#else

#include "minispdlog/sinks/qt_sink.h"
#include "minispdlog/pattern_formatter.h"

#include <QApplication>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>

#include <memory>
#include <string>

using namespace minispdlog;

namespace {

int& qt_argc() {
    static int argc = 1;
    return argc;
}
char arg0[] = "minispdlog_qt_tests";
char* qt_argv[] = {arg0, nullptr};

QApplication& ensure_app() {
    // offscreen：无显示器的 CI / WSL 也能跑
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication app(qt_argc(), qt_argv);
    return app;
}

// 处理挂起的 QueuedConnection 投递
void flush_events() {
    QApplication::processEvents();
    QTimer::singleShot(0, &QApplication::quit);
    // 再泵一轮，确保 invokeMethod 完成
    for (int i = 0; i < 20; ++i) {
        QApplication::processEvents();
    }
}

} // namespace

TEST_CASE("qt_sink appends to QTextEdit [sink][qt]") {
    auto& app = ensure_app();
    (void)app;

    QTextEdit edit;
    auto sink = std::make_shared<sinks::qt_sink_mt>(&edit, "append");
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));

    logger lg("qt_edit", sink);
    lg.info("from qt_sink");
    flush_events();

    const QString text = edit.toPlainText();
    REQUIRE(text.contains("from qt_sink"));
}

TEST_CASE("qt_sink appends to QPlainTextEdit [sink][qt]") {
    auto& app = ensure_app();
    (void)app;

    QPlainTextEdit edit;
    auto sink = std::make_shared<sinks::qt_sink_mt>(&edit, "appendPlainText");
    sink->set_formatter(std::make_unique<pattern_formatter>("%v"));

    logger lg("qt_plain", sink);
    lg.warn("plain line");
    flush_events();

    REQUIRE(edit.toPlainText().contains("plain line"));
}

TEST_CASE("qt_logger_mt factory registers logger [sink][qt][factory]") {
    auto& app = ensure_app();
    (void)app;

    QTextEdit edit;
    auto lg = qt_logger_mt("qt_factory", &edit, "append");
    REQUIRE(lg != nullptr);
    REQUIRE(registry::instance().get("qt_factory") != nullptr);

    auto sinks = lg->sinks();
    REQUIRE_FALSE(sinks.empty());
    sinks[0]->set_formatter(std::make_unique<pattern_formatter>("%v"));

    lg->info("factory ok");
    flush_events();
    REQUIRE(edit.toPlainText().contains("factory ok"));

    registry::instance().drop("qt_factory");
}

TEST_CASE("qt_sink rejects null object [sink][qt]") {
    REQUIRE_THROWS_AS(
        std::make_shared<sinks::qt_sink_mt>(nullptr, "append"),
        std::invalid_argument);
}

#endif // MINISPDLOG_WITH_QT
