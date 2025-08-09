#include <cuteplayer/main.hpp>
#include <string>

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;

// TODO: break 是否导致内存泄漏隐患?

int main(int argc, char* argv[]) {
    InitLogger();

    if (argc < 2) {
        LOG_ERROR("Usage: {} <file>", argv[0]);
        return -1;
    }

    std::string input_file{argv[1]};

    int sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    if (SDL_Init(sdl_init_flags)) {
        LOG_ERROR("初始化 SDL 失败: {}", SDL_GetError());
        return -1;
    }

    window =
        SDL_CreateWindow("CutePlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         kDefaultWidth, kDefaultHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!window || !renderer) {
        LOG_ERROR("创建窗口或渲染器失败: {}", SDL_GetError());
        return -1;
    }

    AVState* video_state = OpenStream(input_file);

    if (!video_state) {
        LOG_ERROR("打开流失败");
        return -1;
    }
    // 监听键盘鼠标事件
    SdlEventLoop(video_state);
    return 0;
}
