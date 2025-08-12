#include <cuteplayer/logger.hpp>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

void init_logger(std::string_view log_file_path, std::string_view level) {
    try {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        sinks.push_back(
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path.data(), true));
        auto logger = std::make_shared<spdlog::logger>("CutePlayer", sinks.begin(), sinks.end());
        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);
        logger->set_level(spdlog::level::from_str(level.data()));
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [thread %t] %v");
        LOG_INFO("Logger 初始化成功! 日志级别: {}", level.data());
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Logger 初始化失败: " << ex.what() << std::endl;
    }
}