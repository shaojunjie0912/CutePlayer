#pragma once

// ================== spdlog ==================
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// ================== FFmpeg & SDL ==================
extern "C" {
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

// ================== Project Headers ==================
#include <cuteplayer/raii.hpp>

// ================== Logging ==================
void init_logger();

#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)

// ================== Constants ==================
namespace cuteplayer {

constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;
constexpr int kMaxFrameQueueSize = 3;                       // 3 帧
constexpr int kMaxPacketQueueDataBytes = 15 * 1024 * 1024;  // 15 MB
constexpr int kSdlAudioBufferSize = 1024;
constexpr double kMaxAvSyncThreshold = 0.1;
constexpr double kMinAvSyncThreshold = 0.04;
constexpr double kAvNoSyncThreshold = 10.0;
constexpr int kFFRefreshEvent = SDL_USEREVENT + 1;

}  // namespace cuteplayer