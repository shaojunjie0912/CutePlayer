#pragma once

#include <cuteplayer/ffmpeg.hpp>

constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 540;
// constexpr int kScreenLeft = SDL_WINDOWPOS_CENTERED; // 窗口左上角的 x 坐标
// constexpr int kScreenTop = SDL_WINDOWPOS_CENTERED;  // 窗口左上角的 y 坐标
constexpr int kVideoPictureQueueSize = 3;
constexpr int kFFRefreshEvent = SDL_USEREVENT + 1;
constexpr int kMaxQueueSize = 15 * 1024 * 1024;
constexpr int kSdlAudioBufferSize = 1024;
constexpr double kMaxAvSyncThreshold = 0.1;
constexpr double kMinAvSyncThreshold = 0.04;
constexpr double kAvNoSyncThreshold = 10.0;
constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 540;