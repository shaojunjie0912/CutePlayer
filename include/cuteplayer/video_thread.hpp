#pragma once

#include <cuteplayer/common.hpp>
#include <cuteplayer/const.hpp>
#include <cuteplayer/core.hpp>

int DecodeThread(void* arg);

void SdlEventLoop(VideoState* video_state);