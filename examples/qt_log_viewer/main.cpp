/**
 * 最小 Qt 日志查看器示例：
 *   QTextEdit + qt_logger_mt + 后台线程打日志
 *
 * 构建（需 -DMINISPDLOG_WITH_QT=ON）：
 *   cmake --build build --target qt_log_viewer
 */
#include "minispdlog/minispdlog.h"
#include "minispdlog/sinks/qt_sink.h"
#include "minispdlog/pattern_formatter.h"

#include <QApplication>
#include <QMainWindow>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QThread>

#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    auto* window = new QMainWindow;
    auto* central = new QWidget(window);
    auto* layout = new QVBoxLayout(central);
    auto* edit = new QTextEdit(central);
    auto* btn = new QPushButton("Spawn background logs", central);
    edit->setReadOnly(true);
    layout->addWidget(edit);
    layout->addWidget(btn);
    window->setCentralWidget(central);
    window->resize(720, 480);
    window->setWindowTitle("minispdlog Qt viewer");

    auto logger = minispdlog::qt_logger_mt("gui", edit, "append");
    logger->sinks()[0]->set_formatter(
        std::make_unique<minispdlog::pattern_formatter>("[%H:%M:%S] [%l] %v"));

    logger->info("Qt log viewer ready");

    QObject::connect(btn, &QPushButton::clicked, [logger]() {
        // 在非 GUI 线程写日志，验证 AutoConnection 投递
        std::thread([logger]() {
            for (int i = 0; i < 20; ++i) {
                logger->info("background message {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            logger->warn("background batch done");
        }).detach();
    });

    window->show();
    return app.exec();
}
