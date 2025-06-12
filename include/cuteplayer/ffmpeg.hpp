#pragma once

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