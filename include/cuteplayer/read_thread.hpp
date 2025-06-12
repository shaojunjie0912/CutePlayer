#pragma once

#include <cmath>
#include <cstdint>
#include <cuteplayer/common.hpp>
#include <cuteplayer/const.hpp>
#include <cuteplayer/core.hpp>
#include <cuteplayer/ffmpeg.hpp>
#include <string>

// 视频线程
#include <cuteplayer/video_thread.hpp>
// 音频线程
#include <cuteplayer/audio_thread.hpp>

VideoState* OpenStream(std::string const& file_name);

int ReadThread(void* arg);