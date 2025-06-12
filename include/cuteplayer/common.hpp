#pragma once

#include <cuteplayer/const.hpp>
#include <cuteplayer/core.hpp>

// ==================  ==================

void RefreshSchedule(VideoState* video_state, int delay);

void SetDefaultWindowSize(int width, int height, AVRational sar);

void CalculateDisplayRect(SDL_Rect* rect, int screen_x_left, int screen_y_top, int screen_width,
                          int screen_height, int picture_width, int picture_height, AVRational picture_sar);