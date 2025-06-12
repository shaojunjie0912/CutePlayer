#pragma once

#include <cmath>
#include <cuteplayer/common.hpp>
#include <cuteplayer/const.hpp>
#include <cuteplayer/core.hpp>
#include <cuteplayer/ffmpeg.hpp>

int OpenAudio(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate);