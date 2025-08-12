#pragma once

// ================== spdlog ==================
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// ================== Logging ==================
void init_logger(std::string_view log_file_path, std::string_view level);

#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)