#include <cuteplayer/main.hpp>

void InitLogger() {
    // 创建彩色控制台输出
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    // 设置日志格式
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    // 创建默认logger
    auto logger = std::make_shared<spdlog::logger>("CutePlayer", console_sink);
    // 设置日志级别为INFO（对应原来的av_log_set_level(AV_LOG_INFO)）
    logger->set_level(spdlog::level::info);
    // 设置为默认logger
    spdlog::set_default_logger(logger);
    // 立即刷新日志
    spdlog::flush_on(spdlog::level::info);
    LOG_INFO("Logger initialized");
}
