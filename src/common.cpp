#include <cuteplayer/common.hpp>

uint32_t MyRefreshTimerCallback(uint32_t, void* opaque) {
    SDL_Event event;
    event.type = kFFRefreshEvent;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);  // 插入 SDL 事件队列
    return 0;
}

void RefreshSchedule(VideoState* video_state, int delay) {
    //
    SDL_AddTimer(delay, MyRefreshTimerCallback, video_state);
}

void CalculateDisplayRect(SDL_Rect* rect, int screen_x_left, int screen_y_top, int screen_width,
                          int screen_height, int picture_width, int picture_height, AVRational picture_sar) {
    // NOTE: picture_sar: sample aspect ratio 图片的像素宽高比(即图像每个像素的宽高比)
    // 不同于 DAR(display aspect ratio) 显示的宽高比
    AVRational aspect_ratio = picture_sar;
    int64_t width, height, x, y;

    // 如果 pic_sar 为零或负值（即无效值），则将 aspect_ratio 设置为 1:1，表示无畸变的方形像素。
    if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0) {
        aspect_ratio = av_make_q(1, 1);
    }

    // 计算显示的宽高比, 根据图像原始尺寸和像素宽高比计算
    // (显示的宽高比 = 图像的宽高比 * 图像的宽度 / 图像的高度)
    aspect_ratio = av_mul_q(aspect_ratio, av_make_q(picture_width, picture_height));

    // 计算显示的宽高
    height = screen_height;
    width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
    if (width > screen_width) {
        width = screen_width;
        height = av_rescale(width, aspect_ratio.den, aspect_ratio.num) & ~1;
    }

    // 计算显示的位置
    x = (screen_width - width) / 2;
    y = (screen_height - height) / 2;

    rect->x = screen_x_left + x;
    rect->y = screen_y_top + y;

    rect->w = FFMAX((int)width, 1);
    rect->h = FFMAX((int)height, 1);
}

void SetDefaultWindowSize(int width, int height, AVRational sar) {
    SDL_Rect rect;
    // int max_width = screen_width ? screen_width : INT_MAX;
    // int max_height = screen_height ? screen_height : INT_MAX;
    // if (max_width == INT_MAX && max_height == INT_MAX) max_height = height;
    int max_width = kScreenWidth;
    int max_height = kScreenHeight;
    CalculateDisplayRect(&rect, 0, 0, max_width, max_height, width, height, sar);
    // default_width = rect.w;
    // default_height = rect.h;
}