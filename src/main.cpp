#include <cuteplayer/logger.hpp>
#include <cuteplayer/player.hpp>
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    // 1. 设置和解析命令行参数
    cxxopts::Options options(argv[0], "一个基于 SDL2 和 FFmpeg 的简易播放器");
    std::string log_level;
    std::string log_dir;
    std::string media_file;

    // clang-format off
    options.add_options()
      ("h,help", "打印帮助信息")
      ("i,inputfile", "要播放的媒体文件路径", cxxopts::value<std::string>(media_file))
      ("e,loglevel", "设置日志级别 (trace, debug, info, warn, error, critical, off)", cxxopts::value<std::string>()->default_value("info"))
      ("d,logdir", "设置日志目录", cxxopts::value<std::string>()->default_value("logs"));
    // clang-format on

    // 我们需要能够解析位置参数（即没有-f标志的文件名）
    options.parse_positional({"inputfile"});

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    log_level = result["loglevel"].as<std::string>();
    log_dir = result["logdir"].as<std::string>();

    // 初始化日志系统
    std::filesystem::path log_file = log_dir + "/" + log_level + ".log";
    std::filesystem::create_directories(log_file.parent_path());
    init_logger(log_file.string(), log_level);

    // 检查核心参数并运行播放器
    if (!result.count("inputfile")) {
        LOG_ERROR("错误: 未指定要播放的媒体文件!");
        LOG_INFO("用法: {} <文件路径> [选项]", argv[0]);
        LOG_INFO("使用 {} --help 查看更多选项", argv[0]);
        return -1;
    }

    try {
        cuteplayer::Player player{media_file};
        player.Run();
        LOG_INFO("播放器退出!");
    } catch (const std::runtime_error& e) {
        LOG_ERROR("播放器运行失败! 错误信息: {}", e.what());
        return -1;
    }

    return 0;
}