#include <cuteplayer/player.hpp>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    init_logger("logs/debug.log");

    if (argc < 2) {
        LOG_ERROR("Usage: {} <file_path>", argv[0]);
        return -1;
    }

    try {
        cuteplayer::Player player{argv[1]};
        player.Run();
        LOG_INFO("播放器退出!");
    } catch (const std::runtime_error& e) {
        LOG_ERROR("播放器运行失败! 错误信息: {}", e.what());
        SDL_Quit();
        return -1;
    }

    return 0;
}