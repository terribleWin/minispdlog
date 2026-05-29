#include "minispdlog/logger.h"
#include <algorithm>
namespace minispdlog{
    //三种构造函数实现
    logger::logger(std::string name):name_(std::move(name)){}
    logger::logger(std::string name, sinks::sink_ptr single_sink)
        :name_(std::move(name)){
            sinks_.push_back(std::move(single_sink));
        }
    logger::logger(std::string name, std::vector<sinks::sink_ptr> sinks)
        : name_(std::move(name))
        , sinks_(std::move(sinks)){
        }
    void logger::add_sink(sinks::sink_ptr sink) {
    sinks_.push_back(std::move(sink));
    }
    void logger::remove_sink(sinks::sink_ptr sink){
        sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
    }
    std::vector<sinks::sink_ptr>& logger::sinks(){
        return sinks_;
    }        
    const std::vector<sinks::sink_ptr>& logger::sinks() const {
        return sinks_;
    }
    void logger::set_level(level log_level){
        level_ = log_level;
    }
    level logger::get_level() const{
        return level_;
    }
    bool logger::should_log(level msg_level) const {
        return msg_level >= level_;
    }
    void logger::flush() {
        for (auto& sink : sinks_) {
            sink->flush();
        }
    }
    void logger::flush_on(level log_level) {
        flush_level_ = log_level;
    }
    const std::string& logger::name() const {
        return name_;
    }
    void logger::sink_it_(const details::log_msg& msg) {
        for (auto& sink : sinks_) {
            if (sink->should_log(msg.lvl)) {
                sink->log(msg);
            }
        }
        //消息级别>=flush级别则刷新
        if (msg.lvl >= flush_level_) {
            flush();
        }
    }
}