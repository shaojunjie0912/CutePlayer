#include <fmt/core.h>

#include <cuteplayer/read_thread.hpp>
#include <string>

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;

// TODO: 很多break存在内存泄漏隐患
// TODO: return 的话倒也轻松

int main(int argc, char* argv[]) {
    // av_log_set_level(AV_LOG_DEBUG);
    av_log_set_level(AV_LOG_INFO);

    if (argc < 2) {
        av_log(nullptr, AV_LOG_ERROR, "Usage: %s <file>\n", argv[0]);
        return -1;
    }

    std::string input_file{argv[1]};

    int sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (SDL_Init(sdl_init_flags)) {
        av_log(nullptr, AV_LOG_ERROR, "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow("CutePlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kDefaultWidth,
                              kDefaultHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!window || !renderer) {
        av_log(nullptr, AV_LOG_ERROR, "Could not create window or renderer - %s\n", SDL_GetError());
        return -1;
    }

    VideoState* video_state = OpenStream(input_file);

    if (!video_state) {
        av_log(nullptr, AV_LOG_ERROR, "OpenStream failed\n");
        return -1;
    }
    // 监听键盘鼠标事件
    SdlEventLoop(video_state);
    return 0;
}
