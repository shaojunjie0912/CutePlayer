#include <cuteplayer/main.hpp>
#include <cuteplayer/player.hpp>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    init_logger();

    if (argc < 2) {
        LOG_ERROR("Usage: {} <file_path>", argv[0]);
        return -1;
    }

    try {
        cuteplayer::Player player(argv[1]);
        player.RunEventLoop();
    } catch (const std::runtime_error& e) {
        LOG_ERROR("A critical error occurred: {}", e.what());
        // 清理在 player 构造失败前已初始化的资源
        SDL_Quit();
        return -1;
    }

    return 0;
}