#include <cuteplayer/logger.hpp>

void init_logger() {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    auto logger = std::make_shared<spdlog::logger>("CutePlayer", console_sink);
    logger->set_level(spdlog::level::info);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info);
    LOG_INFO("Logger 启动!");
}